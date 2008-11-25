/*
 * gsttiimgenc1.c
 *
 * This file defines the "TIImgenc1" element, which encodes a JPEG image
 *
 * Example usage:
 *     gst-launch videotestsrc num-buffers=1 ! 'video/x-raw-yuv, format=(fourcc)I420, width=720, height=480' ! TIImgenc1 filesink location="<output file>"
 *
 * Notes:
 *    - There is still an issue with the static caps negotiation.  See the
 *      note below by the src pad declaration for more detail.
 *    - search for CEM and look at notes I have there for potential issues
 *
 * Original Author:
 *     Chase Maupin, Texas Instruments, Inc.
 *
 * Copyright (C) $year Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation version 2.1 of the License.
 *
 * This program is distributed #as is# WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <ctype.h>

#include <ti/sdo/dmai/Dmai.h>
#include <ti/sdo/dmai/Cpu.h>
#include <ti/sdo/dmai/Buffer.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/BufTab.h>
#include <ti/sdo/dmai/ce/Ienc1.h>

#include "gsttiimgenc1.h"
#include "gsttidmaibuffertransport.h"
#include "gstticodecs.h"
#include "gsttithreadprops.h"

/* Declare variable used to categorize GST_LOG output */
GST_DEBUG_CATEGORY_STATIC (gst_tiimgenc1_debug);
#define GST_CAT_DEFAULT gst_tiimgenc1_debug

/* Element property identifiers */
enum
{
  PROP_0,
  PROP_ENGINE_NAME,     /* engineName     (string)  */
  PROP_CODEC_NAME,      /* codecName      (string)  */
  PROP_NUM_OUTPUT_BUFS, /* numOutputBufs  (int)     */
  PROP_FRAMERATE,       /* frameRate      (int)     */
  PROP_RESOLUTION,      /* resolution     (string)  */
  PROP_QVALUE,          /* qValue         (int)     */
  PROP_ICOLORSPACE,     /* iColorSpace    (string)  */
  PROP_OCOLORSPACE,     /* oColorSpace    (string)  */
  PROP_DISPLAY_BUFFER,  /* displayBuffer  (boolean) */
  PROP_GEN_TIMESTAMPS   /* genTimeStamps  (boolean) */
};

/* Codec Attributes for conversion function */
enum
{
    VAR_ICOLORSPACE,
    VAR_OCOLORSPACE
};

/* Define sink (input) pad capabilities.  Supported types are:
 *   - UYVY
 *   - 420P
 *   - 422P
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE(
    "rawimage",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    (GST_VIDEO_CAPS_YUV("UYVY")";"
     GST_VIDEO_CAPS_YUV("I420")";"
     GST_VIDEO_CAPS_YUV("Y42B")
    )
);

/* NOTE: There is some issue with the static caps on the output pad.
 *       If they are not defined to any and there are capabilities
 *       on the input buffer then the following error occurs:
 *       erroneous pipeline: could not link tiimgenc10 to filesink0
 *
 *       This needs to be fixed.
 *
 *       The original static caps were defined as:
 *
        static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE(
            "encimage",
            GST_PAD_SRC,
            GST_PAD_ALWAYS,
            GST_STATIC_CAPS
            ("video/x-jpeg, "
                "width=(int)[ 1, MAX ], "
                "height=(int)[ 1, MAX ], "
                "framerate=(fraction)[ 0, MAX ]"
            )
        );
 */
/* Define source (output) pad capabilities */
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE(
    "encimage",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
);

/* Constants */
#define gst_tiimgenc1_CODEC_FREE 0x2

/* Declare a global pointer to our element base class */
static GstElementClass *parent_class = NULL;

/* Static Function Declarations */
static void
 gst_tiimgenc1_base_init(gpointer g_class);
static void
 gst_tiimgenc1_class_init(GstTIImgenc1Class *g_class);
static void
 gst_tiimgenc1_init(GstTIImgenc1 *object, GstTIImgenc1Class *g_class);
static void
 gst_tiimgenc1_set_property (GObject *object, guint prop_id,
     const GValue *value, GParamSpec *pspec);
static void
 gst_tiimgenc1_get_property (GObject *object, guint prop_id, GValue *value,
     GParamSpec *pspec);
static gboolean
 gst_tiimgenc1_set_sink_caps(GstPad *pad, GstCaps *caps);
static gboolean
 gst_tiimgenc1_set_source_caps(GstTIImgenc1 *imgenc1, Buffer_Handle hBuf);
static gboolean
 gst_tiimgenc1_sink_event(GstPad *pad, GstEvent *event);
static GstFlowReturn
 gst_tiimgenc1_chain(GstPad *pad, GstBuffer *buf);
static gboolean
 gst_tiimgenc1_init_image(GstTIImgenc1 *imgenc1);
static gboolean
 gst_tiimgenc1_exit_image(GstTIImgenc1 *imgenc1);
static GstStateChangeReturn
 gst_tiimgenc1_change_state(GstElement *element, GstStateChange transition);
static void*
 gst_tiimgenc1_encode_thread(void *arg);
static void*
 gst_tiimgenc1_queue_thread(void *arg);
static void
 gst_tiimgenc1_broadcast_queue_thread(GstTIImgenc1 *imgenc1);
static void
 gst_tiimgenc1_wait_on_queue_thread(GstTIImgenc1 *imgenc1,
     Int32 waitQueueSize);
static void
 gst_tiimgenc1_drain_pipeline(GstTIImgenc1 *imgenc1);
static GstClockTime
 gst_tiimgenc1_frame_duration(GstTIImgenc1 *imgenc1);
static int 
 gst_tiimgenc1_convert_fourcc(guint32 fourcc);
static int 
 gst_tiimgenc1_convert_attrs(int attr, GstTIImgenc1 *imgenc1);
static char *
 gst_tiimgenc1_codec_color_space_to_str(int cspace);
static int 
 gst_tiimgenc1_codec_color_space_to_fourcc(int cspace);
static int 
 gst_tiimgenc1_convert_color_space(int cspace);


/******************************************************************************
 * gst_tiimgenc1_class_init_trampoline
 *    Boiler-plate function auto-generated by "make_element" script.
 ******************************************************************************/
static void gst_tiimgenc1_class_init_trampoline(gpointer g_class,
                gpointer data)
{
    GST_LOG("Begin\n");
    parent_class = (GstElementClass*) g_type_class_peek_parent(g_class);
    gst_tiimgenc1_class_init((GstTIImgenc1Class*)g_class);
    GST_LOG("Finish\n");
}


/******************************************************************************
 * gst_tiimgenc1_get_type
 *    Boiler-plate function auto-generated by "make_element" script.
 *    Defines function pointers for initialization routines for this element.
 ******************************************************************************/
GType gst_tiimgenc1_get_type(void)
{
    static GType object_type = 0;

    GST_LOG("Begin\n");
    if (G_UNLIKELY(object_type == 0)) {
        static const GTypeInfo object_info = {
            sizeof(GstTIImgenc1Class),
            gst_tiimgenc1_base_init,
            NULL,
            gst_tiimgenc1_class_init_trampoline,
            NULL,
            NULL,
            sizeof(GstTIImgenc1),
            0,
            (GInstanceInitFunc) gst_tiimgenc1_init
        };

        object_type = g_type_register_static((gst_element_get_type()),
                          "GstTIImgenc1", &object_info, (GTypeFlags)0);

        /* Initialize GST_LOG for this object */
        GST_DEBUG_CATEGORY_INIT(gst_tiimgenc1_debug, "TIImgenc1", 0,
            "TI xDM 1.0 Image Encoder");

        GST_LOG("initialized get_type\n");

    }

    GST_LOG("Finish\n");
    return object_type;
};


/******************************************************************************
 * gst_tiimgenc1_base_init
 *    Boiler-plate function auto-generated by "make_element" script.
 *    Initializes element base class.
 ******************************************************************************/
static void gst_tiimgenc1_base_init(gpointer gclass)
{
    static GstElementDetails element_details = {
        "TI xDM 1.0 Image Encoder",
        "Codec/Encoder/Image",
        "Encodes an image using an xDM 1.0-based codec",
        "Chase Maupin; Texas Instruments, Inc."
    };
    GST_LOG("Begin\n");

    GstElementClass *element_class = GST_ELEMENT_CLASS(gclass);

    gst_element_class_add_pad_template(element_class,
        gst_static_pad_template_get (&src_factory));
    gst_element_class_add_pad_template(element_class,
        gst_static_pad_template_get (&sink_factory));
    gst_element_class_set_details(element_class, &element_details);
    GST_LOG("Finish\n");
}


/******************************************************************************
 * gst_tiimgenc1_class_init
 *    Boiler-plate function auto-generated by "make_element" script.
 *    Initializes the TIImgenc1 class.
 ******************************************************************************/
static void gst_tiimgenc1_class_init(GstTIImgenc1Class *klass)
{
    GObjectClass    *gobject_class;
    GstElementClass *gstelement_class;

    gobject_class    = (GObjectClass*)    klass;
    gstelement_class = (GstElementClass*) klass;

    GST_LOG("Begin\n");

    gobject_class->set_property = gst_tiimgenc1_set_property;
    gobject_class->get_property = gst_tiimgenc1_get_property;

    gstelement_class->change_state = gst_tiimgenc1_change_state;

    g_object_class_install_property(gobject_class, PROP_ENGINE_NAME,
        g_param_spec_string("engineName", "Engine Name",
            "Engine name used by Codec Engine", "unspecified",
            G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_CODEC_NAME,
        g_param_spec_string("codecName", "Codec Name", "Name of image codec",
            "unspecified", G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_QVALUE,
        g_param_spec_int("qValue",
            "qValue for encoder",
            "Q compression factor, from 1 (lowest quality)\n"
            "to 97 (highest quality). [default: 75]\n",
            1, 97, 75, G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_ICOLORSPACE,
        g_param_spec_string("iColorSpace", "Input Color Space",
            "Colorspace of the input image\n"
            "\tYUV422P, YUV420P, UYVY",
            "UYVY", G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_OCOLORSPACE,
        g_param_spec_string("oColorSpace", "Output Color Space",
            "Colorspace of the output image\n"
            "\tYUV422P, YUV420P, UYVY",
            "YUV422P", G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_RESOLUTION,
        g_param_spec_string("resolution", "Image Resolution",
            "The resolution of the input image ('width'x'height')\n",
            "720x480", G_PARAM_WRITABLE));

    /* We allow more that one output buffer because this is the buffer that
     * is sent to the downstream element.  It may be that we need to have
     * more than 1 buffer for the mjpeg case if the downstream element
     * doesn't give the buffer back in time for the codec to use.
     */
    g_object_class_install_property(gobject_class, PROP_NUM_OUTPUT_BUFS,
        g_param_spec_int("numOutputBufs",
            "Number of Ouput Buffers",
            "Number of output buffers to allocate for codec",
            1, G_MAXINT32, 1, G_PARAM_WRITABLE));

    /* For MJPEG we will communicate a frame rate to the down stream
     * element.  This can be ignored normally.
     */
    g_object_class_install_property(gobject_class, PROP_FRAMERATE,
        g_param_spec_int("frameRate",
            "Frame rate to play output",
            "Communicate this framerate to downstream elements.  The frame "
            "rate specified should be an integer.  If 29.97fps is desired, "
            "specify 30 for this setting",
            1, G_MAXINT32, 30, G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_DISPLAY_BUFFER,
        g_param_spec_boolean("displayBuffer", "Display Buffer",
            "Display circular buffer status while processing",
            FALSE, G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_GEN_TIMESTAMPS,
        g_param_spec_boolean("genTimeStamps", "Generate Time Stamps",
            "Set timestamps on output buffers",
            TRUE, G_PARAM_WRITABLE));

    GST_LOG("Finish\n");
}


/******************************************************************************
 * gst_tiimgenc1_init
 *    Initializes a new element instance, instantiates pads and sets the pad
 *    callback functions.
 ******************************************************************************/
static void gst_tiimgenc1_init(GstTIImgenc1 *imgenc1, GstTIImgenc1Class *gclass)
{
    GST_LOG("Begin\n");
    /* Instantiate encoded image sink pad */
    imgenc1->sinkpad =
        gst_pad_new_from_static_template(&sink_factory, "rawimage");
    gst_pad_set_setcaps_function(
        imgenc1->sinkpad, GST_DEBUG_FUNCPTR(gst_tiimgenc1_set_sink_caps));
    gst_pad_set_getcaps_function(
        imgenc1->sinkpad, GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));
    gst_pad_set_event_function(
        imgenc1->sinkpad, GST_DEBUG_FUNCPTR(gst_tiimgenc1_sink_event));
    gst_pad_set_chain_function(
        imgenc1->sinkpad, GST_DEBUG_FUNCPTR(gst_tiimgenc1_chain));

    /* Instantiate deceoded image source pad */
    imgenc1->srcpad =
        gst_pad_new_from_static_template(&src_factory, "encimage");
    gst_pad_set_getcaps_function(
        imgenc1->srcpad, GST_DEBUG_FUNCPTR(gst_pad_proxy_getcaps));

    /* Add pads to TIImgenc1 element */
    gst_element_add_pad(GST_ELEMENT(imgenc1), imgenc1->sinkpad);
    gst_element_add_pad(GST_ELEMENT(imgenc1), imgenc1->srcpad);

    /* Initialize TIImgenc1 state */
    imgenc1->engineName        = NULL;
    imgenc1->codecName         = NULL;
    imgenc1->displayBuffer     = FALSE;
    imgenc1->genTimeStamps     = FALSE;
    imgenc1->iColor            = NULL;
    imgenc1->oColor            = NULL;
    imgenc1->qValue            = 0;
    imgenc1->width             = 0;
    imgenc1->height            = 0;

    imgenc1->hEngine           = NULL;
    imgenc1->hIe               = NULL;
    imgenc1->drainingEOS       = FALSE;
    imgenc1->threadStatus      = 0UL;
    imgenc1->capsSet           = FALSE;

    imgenc1->encodeDrained     = FALSE;
    imgenc1->waitOnEncodeDrain = NULL;

    imgenc1->hInFifo           = NULL;

    imgenc1->waitOnQueueThread = NULL;
    imgenc1->waitQueueSize     = 0;

    imgenc1->framerateNum      = 0;
    imgenc1->framerateDen      = 0;

    imgenc1->numOutputBufs     = 0UL;
    imgenc1->hOutBufTab        = NULL;
    imgenc1->circBuf           = NULL;

    GST_LOG("Finish\n");
}

/*******************************************************************************
 * gst_tiimgenc1_string_cap
 *    This function will capitalize the given string.  This makes it easier
 *    to compare the strings later.
*******************************************************************************/
static void gst_tiimgenc1_string_cap(gchar *str) {
    int i;
    int len = strlen(str);

    GST_LOG("Begin\n");
    for (i=0; i<len; i++) {
        str[i] = (char)toupper(str[i]);
    }
    GST_LOG("Finish\n");
    return;
}

/******************************************************************************
 * gst_tiimgenc1_set_property
 *     Set element properties when requested.
 ******************************************************************************/
static void gst_tiimgenc1_set_property(GObject *object, guint prop_id,
                const GValue *value, GParamSpec *pspec)
{
    GstTIImgenc1 *imgenc1 = GST_TIIMGENC1(object);

    GST_LOG("Begin\n");

    switch (prop_id) {
        case PROP_ENGINE_NAME:
            if (imgenc1->engineName) {
                g_free((gpointer)imgenc1->engineName);
            }
            imgenc1->engineName =
                (gchar*)g_malloc(strlen(g_value_get_string(value)) + 1);
            strcpy((gchar *)imgenc1->engineName, g_value_get_string(value));
            GST_LOG("setting \"engineName\" to \"%s\"\n", imgenc1->engineName);
            break;
        case PROP_CODEC_NAME:
            if (imgenc1->codecName) {
                g_free((gpointer)imgenc1->codecName);
            }
            imgenc1->codecName =
                (gchar*)g_malloc(strlen(g_value_get_string(value)) + 1);
            strcpy((gchar*)imgenc1->codecName, g_value_get_string(value));
            GST_LOG("setting \"codecName\" to \"%s\"\n", imgenc1->codecName);
            break;
        case PROP_RESOLUTION:
            sscanf(g_value_get_string(value), "%dx%d", &imgenc1->width,
                    &imgenc1->height);
            GST_LOG("setting \"resolution\" to \"%dx%d\"\n",
                imgenc1->width, imgenc1->height);
            break;
        case PROP_QVALUE:
            imgenc1->qValue = g_value_get_int(value);
            GST_LOG("setting \"qValue\" to \"%d\"\n",
                imgenc1->qValue);
            break;
        case PROP_OCOLORSPACE:
            imgenc1->oColor = g_strdup(g_value_get_string(value));
            gst_tiimgenc1_string_cap(imgenc1->oColor);
            GST_LOG("setting \"oColor\" to \"%s\"\n",
                imgenc1->oColor);
            break;
        case PROP_ICOLORSPACE:
            imgenc1->iColor = g_strdup(g_value_get_string(value));
            gst_tiimgenc1_string_cap(imgenc1->iColor);
            GST_LOG("setting \"iColor\" to \"%s\"\n",
                imgenc1->iColor);
            break;
        case PROP_NUM_OUTPUT_BUFS:
            imgenc1->numOutputBufs = g_value_get_int(value);
            GST_LOG("setting \"numOutputBufs\" to \"%d\"\n",
                imgenc1->numOutputBufs);
            break;
        case PROP_FRAMERATE:
        {
            imgenc1->framerateNum = g_value_get_int(value);
            imgenc1->framerateDen = 1;

            /* If 30fps was specified, use 29.97 */
            if (imgenc1->framerateNum == 30) {
                imgenc1->framerateNum = 30000;
                imgenc1->framerateDen = 1001;
            }

            GST_LOG("setting \"frameRate\" to \"%2.2d\"\n",
                (gdouble)imgenc1->framerateNum /
                (gdouble)imgenc1->framerateDen);
            break;
        }
        case PROP_DISPLAY_BUFFER:
            imgenc1->displayBuffer = g_value_get_boolean(value);
            GST_LOG("setting \"displayBuffer\" to \"%s\"\n",
                imgenc1->displayBuffer ? "TRUE" : "FALSE");
            break;
        case PROP_GEN_TIMESTAMPS:
            imgenc1->genTimeStamps = g_value_get_boolean(value);
            GST_LOG("setting \"genTimeStamps\" to \"%s\"\n",
                imgenc1->genTimeStamps ? "TRUE" : "FALSE");
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }

    GST_LOG("Finish\n");
}

/******************************************************************************
 * gst_tiimgenc1_get_property
 *     Return values for requested element property.
 ******************************************************************************/
static void gst_tiimgenc1_get_property(GObject *object, guint prop_id,
                GValue *value, GParamSpec *pspec)
{
    GstTIImgenc1 *imgenc1 = GST_TIIMGENC1(object);

    GST_LOG("Begin\n");

    switch (prop_id) {
        case PROP_ENGINE_NAME:
            g_value_set_string(value, imgenc1->engineName);
            break;
        case PROP_CODEC_NAME:
            g_value_set_string(value, imgenc1->codecName);
            break;
        case PROP_QVALUE:
            g_value_set_int(value, imgenc1->qValue);
            break;
        case PROP_OCOLORSPACE:
            g_value_set_string(value, imgenc1->oColor);
            break;
        case PROP_ICOLORSPACE:
            g_value_set_string(value, imgenc1->iColor);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }

    GST_LOG("Finish\n");
}

/*******************************************************************************
 * gst_tiimgenc1_set_sink_caps_helper
 *     This function will look at the capabilities given and set the values
 *     for the encoder if they were not specified on the command line.
 *     It returns TRUE if everything passes and FALSE if there is no
 *     capability in the buffer and the value was not specified on the
 *     command line.
 ******************************************************************************/
static gboolean gst_tiimgenc1_set_sink_caps_helper(GstPad *pad, GstCaps *caps)
{
    GstTIImgenc1 *imgenc1;
    GstStructure *capStruct;
    Cpu_Device    device;
    const gchar  *mime;
    GstTICodec   *codec = NULL;

    GST_LOG("Begin\n");

    imgenc1   = GST_TIIMGENC1(gst_pad_get_parent(pad));

    /* Determine which device the application is running on */
    if (Cpu_getDevice(NULL, &device) < 0) {
        GST_ERROR("Failed to determine target board\n");
        return FALSE;
    }

    /* Set the default values */
    imgenc1->params = Ienc1_Params_DEFAULT;
    imgenc1->dynParams = Ienc1_DynamicParams_DEFAULT;

    #if defined(Platform_omap3530)
    if (device == Cpu_Device_OMAP3530) {
        imgenc1->params.dataEndianness = XDM_LE_32;
    }
    #endif

    /* Set codec to JPEG Encoder */
    codec = gst_ticodec_get_codec("JPEG Image Encoder");

    /* Report if the required codec was not found */
    if (!codec) {
        GST_ERROR("unable to find codec needed for stream");
        return FALSE;
    }

    /* Configure the element to use the detected engine name and codec, unless
     * they have been using the set_property function.
     */
    if (!imgenc1->engineName) {
        imgenc1->engineName = codec->CE_EngineName;
    }
    if (!imgenc1->codecName) {
        imgenc1->codecName = codec->CE_CodecName;
    }

    if (!caps) {
        GST_INFO("No caps on input.  Using command line values");
        imgenc1->capsSet = TRUE;
        return TRUE;
    }

    capStruct = gst_caps_get_structure(caps, 0);
    mime      = gst_structure_get_name(capStruct);

    GST_INFO("requested sink caps:  %s", gst_caps_to_string(caps));

    /* Generic Video Properties */
    if (!strncmp(mime, "video/", 6)) {
        gint  framerateNum;
        gint  framerateDen;
        gint  width;
        gint  height;
   
        if (!imgenc1->framerateNum) {        
            if (gst_structure_get_fraction(capStruct, "framerate", 
                                &framerateNum, &framerateDen)) {
                imgenc1->framerateNum = framerateNum;
                imgenc1->framerateDen = framerateDen;
            }
        }

        /* Get the width and height of the frame if available */
        if (!imgenc1->width)
            if (gst_structure_get_int(capStruct, "width", &width)) 
                imgenc1->params.maxWidth = 
                imgenc1->dynParams.inputWidth = width;
        if (!imgenc1->height)
            if (gst_structure_get_int(capStruct, "height", &height))
                imgenc1->params.maxHeight = 
                imgenc1->dynParams.inputHeight = height;
    }

    /* Get the Chroma Format */
    if (!strcmp(mime, "video/x-raw-yuv")) {
        guint32      format;

        /* Retreive input Color Format from the buffer properties unless
         * a value was set on the command line.
         */
        if (!imgenc1->iColor) {
            if (gst_structure_get_fourcc(capStruct, "format", &format)) {
                imgenc1->dynParams.inputChromaFormat =
                    gst_tiimgenc1_convert_fourcc(format);
            }
            else {
                GST_ERROR("Input chroma format not specified on either "
                          "the command line with iColorFormat or in "
                          "the buffer caps");
                return FALSE;
            }
        }

        /* If an output color format is not specified use the input color
         * format.
         */
        if (!imgenc1->oColor) {
            imgenc1->params.forceChromaFormat = 
                        imgenc1->dynParams.inputChromaFormat;
        }
    } else {
        /* Mime type not supported */
        GST_ERROR("stream type not supported");
        return FALSE;
    }

    /* Shut-down any running image encoder */
    if (!gst_tiimgenc1_exit_image(imgenc1)) {
        GST_ERROR("unable to shut-down running image encoder");
        return FALSE;
    }

    /* Flag that we have run this code at least once */
    imgenc1->capsSet = TRUE;

    GST_LOG("Finish\n");
    return TRUE;
}

/******************************************************************************
 * gst_tiimgenc1_set_sink_caps
 *     Negotiate our sink pad capabilities.
 ******************************************************************************/
static gboolean gst_tiimgenc1_set_sink_caps(GstPad *pad, GstCaps *caps)
{
    GstTIImgenc1 *imgenc1;
    imgenc1   = GST_TIIMGENC1(gst_pad_get_parent(pad));

    GST_LOG("Begin\n");

    /* If this call fails then unref the gobject */
    if (!gst_tiimgenc1_set_sink_caps_helper(pad, caps)) {
        GST_ERROR("stream type not supported");
        gst_object_unref(imgenc1);
        return FALSE;
    }

    GST_LOG("sink caps negotiation successful\n");
    GST_LOG("Finish\n");
    return TRUE;
}


/******************************************************************************
 * gst_tiimgenc1_set_source_caps
 *     Negotiate our source pad capabilities.
 ******************************************************************************/
static gboolean gst_tiimgenc1_set_source_caps(
                    GstTIImgenc1 *imgenc1, Buffer_Handle hBuf)
{
    BufferGfx_Dimensions  dim;
    GstCaps              *caps;
    gboolean              ret;
    GstPad               *pad;
    guint32              format;

    GST_LOG("Begin\n");
    pad = imgenc1->srcpad;

    /* Create a UYVY caps object using the dimensions from the given buffer */
    BufferGfx_getDimensions(hBuf, &dim);

    format = gst_tiimgenc1_codec_color_space_to_fourcc(imgenc1->params.forceChromaFormat);

    if (format == -1) {
        GST_ERROR("Could not convert codec color space to fourcc");
        return FALSE;
    }
    caps =
        gst_caps_new_simple("video/x-jpeg",
            "format",    GST_TYPE_FOURCC,   format,
            "framerate", GST_TYPE_FRACTION, imgenc1->framerateNum,
                                            imgenc1->framerateDen,
            "width",     G_TYPE_INT,        dim.width,
            "height",    G_TYPE_INT,        dim.height,
            NULL);

    /* Set the source pad caps */
    GST_LOG("setting source caps to UYVY:  %s", gst_caps_to_string(caps));
    ret = gst_pad_set_caps(pad, caps);
    gst_caps_unref(caps);

    GST_LOG("Finish\n");
    return ret;
}


/******************************************************************************
 * gst_tiimgenc1_sink_event
 *     Perform event processing on the input stream.  At the moment, this
 *     function isn't needed as this element doesn't currently perform any
 *     specialized event processing.  We'll leave it in for now in case we need
 *     it later on.
 ******************************************************************************/
static gboolean gst_tiimgenc1_sink_event(GstPad *pad, GstEvent *event)
{
    GstTIImgenc1 *imgenc1;
    gboolean      ret;

    GST_LOG("Begin\n");

    imgenc1 = GST_TIIMGENC1(GST_OBJECT_PARENT(pad));

    GST_DEBUG("pad \"%s\" received:  %s\n", GST_PAD_NAME(pad),
        GST_EVENT_TYPE_NAME(event));

    switch (GST_EVENT_TYPE(event)) {

        case GST_EVENT_NEWSEGMENT:
            /* maybe save and/or update the current segment (e.g. for output
             * clipping) or convert the event into one in a different format
             * (e.g. BYTES to TIME) or drop it and set a flag to send a
             * newsegment event in a different format later
             */
            ret = gst_pad_push_event(imgenc1->srcpad, event);
            break;

        case GST_EVENT_EOS:
            /* end-of-stream: process any remaining encoded frame data */
            GST_LOG("no more input; draining remaining encoded image data\n");

            if (!imgenc1->drainingEOS) {
               gst_tiimgenc1_drain_pipeline(imgenc1);
             }

            /* Propagate EOS to downstream elements */
            ret = gst_pad_push_event(imgenc1->srcpad, event);
            break;

        case GST_EVENT_FLUSH_STOP:
            ret = gst_pad_push_event(imgenc1->srcpad, event);
            break;

        /* Unhandled events */
        case GST_EVENT_BUFFERSIZE:
        case GST_EVENT_CUSTOM_BOTH:
        case GST_EVENT_CUSTOM_BOTH_OOB:
        case GST_EVENT_CUSTOM_DOWNSTREAM:
        case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
        case GST_EVENT_CUSTOM_UPSTREAM:
        case GST_EVENT_FLUSH_START:
        case GST_EVENT_NAVIGATION:
        case GST_EVENT_QOS:
        case GST_EVENT_SEEK:
        case GST_EVENT_TAG:
        default:
            ret = gst_pad_event_default(pad, event);
            break;

    }

    GST_LOG("Finish\n");
    return ret;

}


/******************************************************************************
 * gst_tiimgenc1_chain
 *    This is the main processing routine.  This function receives a buffer
 *    from the sink pad, processes it, and pushes the result to the source
 *    pad.
 ******************************************************************************/
static GstFlowReturn gst_tiimgenc1_chain(GstPad * pad, GstBuffer * buf)
{
    GstTIImgenc1 *imgenc1       = GST_TIIMGENC1(GST_OBJECT_PARENT(pad));
    GstCaps      *caps          = GST_BUFFER_CAPS(buf);
    gboolean     checkResult;

    GST_LOG("Begin\n");
    /* If any thread aborted, communicate it to the pipeline */
    if (gst_tithread_check_status(
            imgenc1, TIThread_ANY_ABORTED, checkResult)) {
       gst_buffer_unref(buf);
       return GST_FLOW_UNEXPECTED;
    }

    /* If we have not negotiated the caps at least once then do so now */
    if (!imgenc1->capsSet) {
        if (!gst_tiimgenc1_set_sink_caps_helper(pad, caps)) {
            GST_ERROR("Could not set caps");
            return GST_FLOW_UNEXPECTED;
        }
    }

    /* If our engine handle is currently NULL, then either this is our first
     * buffer or the upstream element has re-negotiated our capabilities which
     * resulted in our engine being closed.  In either case, we need to
     * initialize (or re-initialize) our image encoder to handle the new
     * stream.
     */
    if (imgenc1->hEngine == NULL) {
        if (!gst_tiimgenc1_init_image(imgenc1)) {
            GST_ERROR("unable to initialize image\n");
            return GST_FLOW_UNEXPECTED;
        }

        GST_TICIRCBUFFER_TIMESTAMP(imgenc1->circBuf) =
            GST_CLOCK_TIME_IS_VALID(GST_BUFFER_TIMESTAMP(buf)) ?
            GST_BUFFER_TIMESTAMP(buf) : 0ULL;
    }

    /* Don't queue up too many buffers -- if we collect too many input buffers
     * without consuming them we'll run out of memory.  Once we reach a
     * threshold, block until the queue thread removes some buffers.
     */
    if (Fifo_getNumEntries(imgenc1->hInFifo) > 500) {
        gst_tiimgenc1_wait_on_queue_thread(imgenc1, 400);
    }

    /* Queue up the encoded data stream into a circular buffer */
    if (Fifo_put(imgenc1->hInFifo, buf) < 0) {
        GST_ERROR("Failed to send buffer to queue thread\n");
        return GST_FLOW_UNEXPECTED;
    }

    GST_LOG("Finish\n");

    return GST_FLOW_OK;
}

/*******************************************************************************
 * gst_tiimgenc1_convert_fourcc
 *      This function will take in a fourcc value (as used in the format
 *      member of the input parameters) and convert it into the color
 *      format for the codec to use.
*******************************************************************************/
static int gst_tiimgenc1_convert_fourcc(guint32 fourcc) {
    gchar format[4];

    GST_LOG("Begin\n");
    sprintf(format, "%"GST_FOURCC_FORMAT, GST_FOURCC_ARGS(fourcc));
    GST_DEBUG("format is %s\n", format);

    if (!strcmp(format, "UYVY")) {
        GST_LOG("Finish\n");
        return gst_tiimgenc1_convert_color_space(ColorSpace_UYVY);
    } else if (!strcmp(format, "Y42B")) {
        GST_LOG("Finish\n");
        return gst_tiimgenc1_convert_color_space(ColorSpace_YUV422P);
    } else if (!strcmp(format, "I420")) {
        GST_LOG("Finish\n");
        return gst_tiimgenc1_convert_color_space(ColorSpace_YUV420P);
    } else {
        GST_LOG("Finish\n");
        return -1;
    }
}

/*******************************************************************************
 * gst_tiimgenc1_convert_attrs
 *    This function will convert the human readable strings for the
 *    attributes into the proper integer values for the enumerations.
*******************************************************************************/
static int gst_tiimgenc1_convert_attrs(int attr, GstTIImgenc1 *imgenc1)
{
  int ret;

  GST_DEBUG("Begin\n");
  switch (attr) {
    case VAR_ICOLORSPACE:
      if (!strcmp(imgenc1->iColor, "UYVY"))
          return ColorSpace_UYVY;
      else if (!strcmp(imgenc1->iColor, "YUV420P"))
          return ColorSpace_YUV420P;
      else if (!strcmp(imgenc1->iColor, "YUV422P"))
          return ColorSpace_YUV422P;
      else {
        GST_ERROR("Invalid iColorSpace entered (%s).  Please choose from:\n"
                "\tUYVY, YUV420P, YUV422P\n", imgenc1->iColor);
        return -1;
      }
    break;
    case VAR_OCOLORSPACE:
      if (!strcmp(imgenc1->oColor, "UYVY"))
          return ColorSpace_UYVY;
      else if (!strcmp(imgenc1->oColor, "YUV420P"))
          return ColorSpace_YUV420P;
      else if (!strcmp(imgenc1->oColor, "YUV422P"))
          return ColorSpace_YUV422P;
      else {
        GST_ERROR("Invalid oColorSpace entered (%s).  Please choose from:\n"
                "\tUYVY, YUV420P, YUV422P\n", imgenc1->oColor);
        return -1;
      }
    break;
    default:
      GST_ERROR("Unknown Attribute\n");
      ret=-1;
      break;
  }
  GST_DEBUG("Finish\n");
  return ret;
}

/*******************************************************************************
 * gst_tiimgenc1_codec_color_space_to_str
 *      Converts the codec color space values to the corresponding
 *      human readable string value
*******************************************************************************/
static char *gst_tiimgenc1_codec_color_space_to_str(int cspace) {
    GST_LOG("Begin");
    switch (cspace) {
        case XDM_YUV_422ILE:
            GST_LOG("Finish");
            return "UYVY";
            break;
        case XDM_YUV_420P:
            GST_LOG("Finish");
            return "YUV420P";
            break;
        case XDM_YUV_422P:
            GST_LOG("Finish");
            return "YUV422P";
            break;
        default:
            GST_ERROR("Unknown xDM color space");
            GST_LOG("Finish");
            return "Unknown";
    }
}

/*******************************************************************************
 * gst_tiimgenc1_codec_color_space_to_fourcc
 *      Converts the codec color space values to the corresponding
 *      fourcc values
*******************************************************************************/
static int gst_tiimgenc1_codec_color_space_to_fourcc(int cspace) {
    GST_LOG("Begin");
    switch (cspace) {
        case XDM_YUV_422ILE:
            GST_LOG("Finish");
            return GST_MAKE_FOURCC('U', 'Y', 'V', 'Y');
            break;
        case XDM_YUV_420P:
            GST_LOG("Finish");
            return GST_MAKE_FOURCC('I', '4', '2', '0');
            break;
        case XDM_YUV_422P:
            GST_LOG("Finish");
            return GST_MAKE_FOURCC('Y', '4', '2', 'B');
            break;
        default:
            GST_ERROR("Unknown xDM color space");
            GST_LOG("Finish");
            return -1;
            break;
    }
}

/*******************************************************************************
 * gst_tiimgenc1_convert_color_space
 *      Convert the DMAI color space type to the corresponding color space
 *      used by the codec.
*******************************************************************************/
static int gst_tiimgenc1_convert_color_space(int cspace) {
    GST_LOG("Begin");
    switch (cspace) {
        case ColorSpace_UYVY:
            GST_LOG("Finish");
            return XDM_YUV_422ILE;
            break;
        case ColorSpace_YUV420P:
            GST_LOG("Finish");
            return XDM_YUV_420P;
            break;
        case ColorSpace_YUV422P:
            GST_LOG("Finish");
            return XDM_YUV_422P;
            break;
        default:
            GST_ERROR("Unsupported Color Space\n");
            GST_LOG("Finish");
            return -1;
    }
}

/*******************************************************************************
 * gst_tiimgenc1_set_codec_attrs
 *     Set the attributes for the encode codec.  The general order is to give
 *     preference to values passed in by the user on the command line,
 *     then check the stream for the information.  If the required information
 *     cannot be determined by either method then error out.
 ******************************************************************************/
static gboolean gst_tiimgenc1_set_codec_attrs(GstTIImgenc1 *imgenc1)
{ 
    char *toColor;
    char *tiColor;
    GST_LOG("Begin\n");

    /* Set ColorSpace */
    imgenc1->dynParams.inputChromaFormat = imgenc1->iColor == NULL ?
                    imgenc1->dynParams.inputChromaFormat :
                    gst_tiimgenc1_convert_color_space(gst_tiimgenc1_convert_attrs(VAR_ICOLORSPACE, imgenc1));
    /* Use the input color format if one was not specified on the
     * command line
     */
    imgenc1->params.forceChromaFormat = imgenc1->oColor == NULL ?
                    imgenc1->dynParams.inputChromaFormat :
                    gst_tiimgenc1_convert_color_space(gst_tiimgenc1_convert_attrs(VAR_OCOLORSPACE, imgenc1));

    /* Set Resolution 
     *
     * NOTE:  We assume that there is only 1 resolution.  i.e. there is
     *        no seperate value being used for input resolution and
     *        output resolution
     */
    imgenc1->params.maxWidth = imgenc1->dynParams.inputWidth = 
                    imgenc1->width == 0 ?
                    imgenc1->params.maxWidth :
                    imgenc1->width;
    imgenc1->params.maxHeight = imgenc1->dynParams.inputHeight = 
                    imgenc1->height == 0 ?
                    imgenc1->params.maxHeight :
                    imgenc1->height;
    /* Set captureWidth to 0 in order to use encoded image width */
    imgenc1->dynParams.captureWidth = 0;

    /* Set qValue (default is 75) */
    imgenc1->dynParams.qValue = imgenc1->qValue == 0 ?
                    imgenc1->dynParams.qValue :
                    imgenc1->qValue;

    /* Check for valid values (NOTE: minimum width and height are 64) */
    if (imgenc1->params.maxWidth < 64) {
        GST_ERROR("The resolution width (%d) is too small.  Must be at least 64\n", imgenc1->params.maxWidth);
        return FALSE;
    }
    if (imgenc1->params.maxHeight < 64) {
        GST_ERROR("The resolution height (%d) is too small.  Must be at least 64\n", imgenc1->params.maxHeight);
        return FALSE;
    }
    if ((imgenc1->dynParams.qValue < 0) || (imgenc1->dynParams.qValue > 100)) {
        GST_ERROR("The qValue (%d) is not withing the range of 0-100\n", imgenc1->dynParams.qValue);
        return FALSE;
    }
    if (imgenc1->params.forceChromaFormat == -1) {
        GST_ERROR("Output Color Format (%s) is not supported\n", imgenc1->oColor);
        return FALSE;
    }
    if (imgenc1->dynParams.inputChromaFormat == -1) {
        GST_ERROR("input Color Format (%s) is not supported\n", imgenc1->iColor);
        return FALSE;
    }

    /* Make the color human readable */
    if (!imgenc1->oColor)
        toColor = gst_tiimgenc1_codec_color_space_to_str(imgenc1->params.forceChromaFormat);
    else
        toColor = (char *)imgenc1->oColor;
    
    if (!imgenc1->iColor)
        tiColor = gst_tiimgenc1_codec_color_space_to_str(imgenc1->dynParams.inputChromaFormat);
    else
        tiColor = (char *)imgenc1->iColor;
    
    GST_DEBUG("\nCodec Parameters:\n"
              "\tparams.maxWidth = %d\n"
              "\tparams.maxHeight = %d\n"
              "\tparams.forceChromaFormat = %d (%s)\n"
              "\nDynamic Parameters:\n"
              "\tdynParams.inputWidth = %d\n"
              "\tdynParams.inputHeight = %d\n"
              "\tdynParams.inputChromaFormat = %d (%s)\n"
              "\tdynParams.qValue = %d\n",
              imgenc1->params.maxWidth, imgenc1->params.maxHeight,
              imgenc1->params.forceChromaFormat, toColor,
              imgenc1->dynParams.inputWidth, imgenc1->dynParams.inputHeight,
              imgenc1->dynParams.inputChromaFormat, tiColor,
              imgenc1->dynParams.qValue);
    GST_LOG("Finish\n");
    return TRUE;
}

/******************************************************************************
 * gst_tiimgenc1_init_image
 *     Initialize or re-initializes the image stream
 ******************************************************************************/
static gboolean gst_tiimgenc1_init_image(GstTIImgenc1 *imgenc1)
{
    BufferGfx_Attrs        gfxAttrs  = BufferGfx_Attrs_DEFAULT;
    Rendezvous_Attrs       rzvAttrs  = Rendezvous_Attrs_DEFAULT;
    Fifo_Attrs             fAttrs    = Fifo_Attrs_DEFAULT;
    struct sched_param     schedParam;
    pthread_attr_t         attr;

    GST_LOG("Begin\n");

    /* If image has already been initialized, shut down previous encoder */
    if (imgenc1->hEngine) {
        if (!gst_tiimgenc1_exit_image(imgenc1)) {
            GST_ERROR("failed to shut down existing image encoder\n");
            return FALSE;
        }
    }

    /* Make sure we know what codec we're using */
    if (!imgenc1->engineName) {
        GST_ERROR("engine name not specified\n");
        return FALSE;
    }

    if (!imgenc1->codecName) {
        GST_ERROR("codec name not specified\n");
        return FALSE;
    }

    /* Open the codec engine */
    GST_LOG("opening codec engine \"%s\"\n", imgenc1->engineName);
    imgenc1->hEngine = Engine_open((Char *) imgenc1->engineName, NULL, NULL);

    if (imgenc1->hEngine == NULL) {
        GST_ERROR("failed to open codec engine \"%s\"\n", imgenc1->engineName);
        return FALSE;
    }
    GST_LOG("opened codec engine \"%s\" successfully\n", imgenc1->engineName);

    if (!gst_tiimgenc1_set_codec_attrs(imgenc1)) {
        GST_ERROR("Error while trying to set the codec attrs\n");
        return FALSE;
    }

    GST_LOG("opening image encoder \"%s\"\n", imgenc1->codecName);
    imgenc1->hIe = Ienc1_create(imgenc1->hEngine, (Char*)imgenc1->codecName,
                      &imgenc1->params, &imgenc1->dynParams);

    if (imgenc1->hIe == NULL) {
        GST_ERROR("failed to create image encoder: %s\n", imgenc1->codecName);
        GST_LOG("closing codec engine\n");
        gst_tiimgenc1_exit_image(imgenc1);
        return FALSE;
    }

    /* Create a circular input buffer */
    imgenc1->circBuf = gst_ticircbuffer_new(Ienc1_getInBufSize(imgenc1->hIe));

    if (imgenc1->circBuf == NULL) {
        GST_ERROR("failed to create circular input buffer\n");
        gst_tiimgenc1_exit_image(imgenc1);
        return FALSE;
    }

    /* Display buffer contents if displayBuffer=TRUE was specified */
    gst_ticircbuffer_set_display(imgenc1->circBuf, imgenc1->displayBuffer);

    /* Define the number of display buffers to allocate.  This number must be
     * at least 1, but should be more if codecs don't return a display buffer
     * after every process call.  If this has not been set via set_property(),
     * default to the value set above based on device type.
     */
    if (imgenc1->numOutputBufs == 0) {
        imgenc1->numOutputBufs = 1;
    }

    /* Create codec output buffers */
    GST_LOG("creating output buffer table\n");
    gfxAttrs.colorSpace     = imgenc1->params.forceChromaFormat;
    gfxAttrs.dim.width      = imgenc1->params.maxWidth;
    gfxAttrs.dim.height     = imgenc1->params.maxHeight;
    gfxAttrs.dim.lineLength = BufferGfx_calcLineLength(
                                  gfxAttrs.dim.width, gfxAttrs.colorSpace);

    gfxAttrs.bAttrs.memParams.align = 128;
    /* Both the codec and the GStreamer pipeline can own a buffer */
    gfxAttrs.bAttrs.useMask = gst_tidmaibuffertransport_GST_FREE |
                              gst_tiimgenc1_CODEC_FREE;

    imgenc1->hOutBufTab =
        BufTab_create(imgenc1->numOutputBufs, Ienc1_getOutBufSize(imgenc1->hIe),
            BufferGfx_getBufferAttrs(&gfxAttrs));

    if (imgenc1->hOutBufTab == NULL) {
        GST_ERROR("failed to create output buffers\n");
        gst_tiimgenc1_exit_image(imgenc1);
        return FALSE;
    }

    /* Initialize thread status management */
    imgenc1->threadStatus = 0UL;
    pthread_mutex_init(&imgenc1->threadStatusMutex, NULL);

    /* Set up the queue fifo */
    imgenc1->hInFifo = Fifo_create(&fAttrs);

    /* Create queue thread */
    if (pthread_create(&imgenc1->queueThread, NULL,
            gst_tiimgenc1_queue_thread, (void*)imgenc1)) {
        GST_ERROR("failed to create queue thread\n");
        gst_tiimgenc1_exit_image(imgenc1);
        return FALSE;
    }
    gst_tithread_set_status(imgenc1, TIThread_QUEUE_CREATED);

    /* Initialize custom thread attributes */
    if (pthread_attr_init(&attr)) {
        GST_WARNING("failed to initialize thread attrs\n");
        gst_tiimgenc1_exit_image(imgenc1);
        return FALSE;
    }

    /* Force the thread to use the system scope */
    if (pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM)) {
        GST_WARNING("failed to set scope attribute\n");
        gst_tiimgenc1_exit_image(imgenc1);
        return FALSE;
    }

    /* Force the thread to use custom scheduling attributes */
    if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) {
        GST_WARNING("failed to set schedule inheritance attribute\n");
        gst_tiimgenc1_exit_image(imgenc1);
        return FALSE;
    }

    /* Set the thread to be fifo real time scheduled */
    if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO)) {
        GST_WARNING("failed to set FIFO scheduling policy\n");
        gst_tiimgenc1_exit_image(imgenc1);
        return FALSE;
    }

    /* Set the display thread priority */
    schedParam.sched_priority = GstTIVideoThreadPriority;
    if (pthread_attr_setschedparam(&attr, &schedParam)) {
        GST_WARNING("failed to set scheduler parameters\n");
        return FALSE;
    }

    /* Create encoder thread */
    if (pthread_create(&imgenc1->encodeThread, &attr,
            gst_tiimgenc1_encode_thread, (void*)imgenc1)) {
        GST_ERROR("failed to create encode thread\n");
        gst_tiimgenc1_exit_image(imgenc1);
        return FALSE;
    }
    gst_tithread_set_status(imgenc1, TIThread_DECODE_CREATED);

    /* Initialize rendezvous objects for making threads wait on conditions */
    imgenc1->waitOnEncodeDrain = Rendezvous_create(100, &rzvAttrs);
    imgenc1->waitOnQueueThread = Rendezvous_create(100, &rzvAttrs);
    imgenc1->drainingEOS       = FALSE;

    GST_LOG("Finish\n");
    return TRUE;
}


/******************************************************************************
 * gst_tiimgenc1_exit_image
 *    Shut down any running image encoder, and reset the element state.
 ******************************************************************************/
static gboolean gst_tiimgenc1_exit_image(GstTIImgenc1 *imgenc1)
{
    void*    thread_ret;
    gboolean checkResult;

    GST_LOG("Begin\n");

    /* Drain the pipeline if it hasn't already been drained */
    if (!imgenc1->drainingEOS) {
       gst_tiimgenc1_drain_pipeline(imgenc1);
     }

    /* Shut down the queue thread */
    if (gst_tithread_check_status(
            imgenc1, TIThread_QUEUE_CREATED, checkResult)) {
        GST_LOG("shutting down queue thread\n");

        /* Unstop the queue thread if needed, and wait for it to finish */
        Fifo_flush(imgenc1->hInFifo);

        if (pthread_join(imgenc1->queueThread, &thread_ret) == 0) {
            if (thread_ret == GstTIThreadFailure) {
                GST_DEBUG("queue thread exited with an error condition\n");
            }
        }
    }

    if (imgenc1->hInFifo) {
        Fifo_delete(imgenc1->hInFifo);
        imgenc1->hInFifo = NULL;
    }

    if (imgenc1->waitOnQueueThread) {
        Rendezvous_delete(imgenc1->waitOnQueueThread);
        imgenc1->waitOnQueueThread = NULL;
    }

    /* Shut down the encode thread */
    if (gst_tithread_check_status(
            imgenc1, TIThread_DECODE_CREATED, checkResult)) {
        GST_LOG("shutting down encode thread\n");

        if (pthread_join(imgenc1->encodeThread, &thread_ret) == 0) {
            if (thread_ret == GstTIThreadFailure) {
                GST_DEBUG("encode thread exited with an error condition\n");
            }
        }
    }

    if (imgenc1->waitOnEncodeDrain) {
        Rendezvous_delete(imgenc1->waitOnEncodeDrain);
        imgenc1->waitOnEncodeDrain = NULL;
    }

    /* Shut down thread status management */
    imgenc1->threadStatus = 0UL;
    pthread_mutex_destroy(&imgenc1->threadStatusMutex);

    /* Shut down remaining items */
    if (imgenc1->hIe) {
        GST_LOG("closing image encoder\n");
        Ienc1_delete(imgenc1->hIe);
        imgenc1->hIe = NULL;
    }

    if (imgenc1->hEngine) {
        GST_LOG("closing codec engine\n");
        Engine_close(imgenc1->hEngine);
        imgenc1->hEngine = NULL;
    }

    if (imgenc1->circBuf) {
        GST_LOG("freeing cicrular input buffer\n");
        gst_ticircbuffer_unref(imgenc1->circBuf);
        imgenc1->circBuf      = NULL;
        imgenc1->framerateNum = 0;
        imgenc1->framerateDen = 0;
    }

    if (imgenc1->hOutBufTab) {
        GST_LOG("freeing output buffers\n");
        BufTab_delete(imgenc1->hOutBufTab);
        imgenc1->hOutBufTab = NULL;
    }

    GST_LOG("Finish\n");
    return TRUE;
}


/******************************************************************************
 * gst_tiimgenc1_change_state
 *     Manage state changes for the image stream.  The gStreamer documentation
 *     states that state changes must be handled in this manner:
 *        1) Handle ramp-up states
 *        2) Pass state change to base class
 *        3) Handle ramp-down states
 ******************************************************************************/
static GstStateChangeReturn gst_tiimgenc1_change_state(GstElement *element,
                                GstStateChange transition)
{
    GstStateChangeReturn  ret    = GST_STATE_CHANGE_SUCCESS;
    GstTIImgenc1          *imgenc1 = GST_TIIMGENC1(element);

    GST_LOG("Begin change_state (%d)\n", transition);

    /* Handle ramp-up state changes */
    switch (transition) {
        case GST_STATE_CHANGE_NULL_TO_READY:
            break;
        default:
            break;
    }

    /* Pass state changes to base class */
    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;

    /* Handle ramp-down state changes */
    switch (transition) {
        case GST_STATE_CHANGE_READY_TO_NULL:
            GST_LOG("State changed from READY to NULL.  Shutting down any"
                    "running image encoders\n");
            /* Shut down any running image encoder */
            if (!gst_tiimgenc1_exit_image(imgenc1)) {
                return GST_STATE_CHANGE_FAILURE;
            }
            break;

        default:
            break;
    }

    GST_LOG("Finish change_state\n");
    return ret;
}


/******************************************************************************
 * gst_tiimgenc1_encode_thread
 *     Call the image codec to process a full input buffer
 ******************************************************************************/
static void* gst_tiimgenc1_encode_thread(void *arg)
{
    GstTIImgenc1           *imgenc1        = GST_TIIMGENC1(gst_object_ref(arg));
    GstBuffer              *encDataWindow  = NULL;
    gboolean               codecFlushed    = FALSE;
    void                   *threadRet      = GstTIThreadSuccess;
    Buffer_Handle          hDummyInputBuf  = NULL;
    BufferGfx_Attrs        gfxAttrs        = BufferGfx_Attrs_DEFAULT;
    Buffer_Handle          hDstBuf;
    Int32                  encDataConsumed;
    GstClockTime           encDataTime;
    GstClockTime           frameDuration;
    Buffer_Handle          hEncDataWindow;
    BufferGfx_Dimensions   dim;
    GstBuffer              *outBuf;
    Int                    ret;

    GST_LOG("Begin\n");
    /* Calculate the duration of a single frame in this stream */
    frameDuration = gst_tiimgenc1_frame_duration(imgenc1);

    /* Main thread loop */
    while (TRUE) {

        /* Obtain an encoded data frame */
        encDataWindow  = gst_ticircbuffer_get_data(imgenc1->circBuf);
        encDataTime    = GST_BUFFER_TIMESTAMP(encDataWindow);
        hEncDataWindow = GST_TIDMAIBUFFERTRANSPORT_DMAIBUF(encDataWindow);

        /* If we received a data frame of zero size, there is no more data to
         * process -- exit the thread.  If we weren't told that we are
         * draining the pipeline, something is not right, so exit with an
         * error.
         */
        if (GST_BUFFER_SIZE(encDataWindow) == 0) {
            GST_LOG("no image data remains\n");
            if (!imgenc1->drainingEOS) {
                goto thread_failure;
            }

            Buffer_delete(hDummyInputBuf);
            goto thread_exit;
        }

        /* Obtain a free output buffer for the encoded data */
        hDstBuf = BufTab_getFreeBuf(imgenc1->hOutBufTab);
        if (hDstBuf == NULL) {
            GST_ERROR("failed to get a free contiguous buffer from BufTab\n");
            goto thread_failure;
        }

        /* Make sure the whole buffer is used for output */
        BufferGfx_resetDimensions(hDstBuf);

        /* Create a BufferGfx object that will point to a reference buffer from
         * The circular buffer.  This is needed for the encoder which requires
         * that the input buffer be a BufferGfx object.
         */
        BufferGfx_getBufferAttrs(&gfxAttrs)->reference = TRUE;
        imgenc1->hInBuf = Buffer_create(Buffer_getSize(hEncDataWindow),
                                BufferGfx_getBufferAttrs(&gfxAttrs));
        Buffer_setUserPtr(imgenc1->hInBuf, Buffer_getUserPtr(hEncDataWindow));
        Buffer_setNumBytesUsed(imgenc1->hInBuf, 
                               Buffer_getSize(imgenc1->hInBuf));

        /* Set the dimensions of the buffer to the resolution */
        BufferGfx_getDimensions(hDstBuf, &dim);
        dim.width = imgenc1->dynParams.inputWidth;
        dim.height = imgenc1->dynParams.inputHeight;
        BufferGfx_setDimensions(imgenc1->hInBuf, &dim);
        BufferGfx_setColorSpace(imgenc1->hInBuf,
                     imgenc1->dynParams.inputChromaFormat); 

        /* Invoke the image encoder */
        GST_LOG("invoking the image encoder\n");
        ret             = Ienc1_process(imgenc1->hIe, imgenc1->hInBuf, hDstBuf);
        encDataConsumed = (codecFlushed) ? 0 :
                          Buffer_getNumBytesUsed(hEncDataWindow);

        /* Delete the temporary Graphics buffer used for the encode process */
        Buffer_delete(imgenc1->hInBuf);

        if (ret < 0) {
            GST_ERROR("failed to encode image buffer\n");
            goto thread_failure;
        }

        /* If no data was used we must have some kind of encode error */
        if (ret == Dmai_EBITERROR && encDataConsumed == 0 && !codecFlushed) {
            GST_ERROR("fatal bit error\n");
            goto thread_failure;
        }

        if (ret > 0) {
            GST_LOG("Ienc1_process returned success code %d\n", ret); 
        }

        /* Release the reference buffer, and tell the circular buffer how much
         * data was consumed.
         */
        ret = gst_ticircbuffer_data_consumed(imgenc1->circBuf, encDataWindow,
                  encDataConsumed);
        encDataWindow = NULL;

        if (!ret) {
            goto thread_failure;
        }


        /* Set the source pad capabilities based on the encoded frame
         * properties.
         */
        gst_tiimgenc1_set_source_caps(imgenc1, hDstBuf);

        /* Create a DMAI transport buffer object to carry a DMAI buffer to
         * the source pad.  The transport buffer knows how to release the
         * buffer for re-use in this element when the source pad calls
         * gst_buffer_unref().
         */
        outBuf = gst_tidmaibuffertransport_new(hDstBuf);
        gst_buffer_set_data(outBuf, GST_BUFFER_DATA(outBuf),
            Buffer_getNumBytesUsed(hDstBuf));
        gst_buffer_set_caps(outBuf, GST_PAD_CAPS(imgenc1->srcpad));

        /* If we have a valid time stamp, set it on the buffer */
        if (imgenc1->genTimeStamps &&
            GST_CLOCK_TIME_IS_VALID(encDataTime)) {
            GST_LOG("image timestamp value: %llu\n", encDataTime);
            GST_BUFFER_TIMESTAMP(outBuf) = encDataTime;
            GST_BUFFER_DURATION(outBuf)  = frameDuration;
        }
        else {
            GST_BUFFER_TIMESTAMP(outBuf) = GST_CLOCK_TIME_NONE;
        }

        /* Tell circular buffer how much time we consumed */
        gst_ticircbuffer_time_consumed(imgenc1->circBuf, frameDuration);

        /* Push the transport buffer to the source pad */
        GST_LOG("pushing display buffer to source pad\n");

        if (gst_pad_push(imgenc1->srcpad, outBuf) != GST_FLOW_OK) {
            GST_DEBUG("push to source pad failed\n");
            goto thread_failure;
        }

        /* Release buffers no longer in use by the codec */
        Buffer_freeUseMask(hDstBuf, gst_tiimgenc1_CODEC_FREE);
    }

thread_failure:

    /* If encDataWindow is non-NULL, something bad happened before we had a
     * chance to release it.  Release it now so we don't block the pipeline.
     * We release it by telling the circular buffer that we're done with it and
     * consumed no data.
     */
    if (encDataWindow) {
        gst_ticircbuffer_data_consumed(imgenc1->circBuf, encDataWindow, 0);
    }

    gst_tithread_set_status(imgenc1, TIThread_DECODE_ABORTED);
    threadRet = GstTIThreadFailure;
    gst_ticircbuffer_consumer_aborted(imgenc1->circBuf);
    Rendezvous_force(imgenc1->waitOnQueueThread);

thread_exit:
 
    imgenc1->encodeDrained = TRUE;
    Rendezvous_force(imgenc1->waitOnEncodeDrain);

    gst_object_unref(imgenc1);

    GST_LOG("Finish image encode_thread (%d)\n", (int)threadRet);
    return threadRet;
}


/******************************************************************************
 * gst_tiimgenc1_queue_thread 
 *     Add an input buffer to the circular buffer            
 ******************************************************************************/
static void* gst_tiimgenc1_queue_thread(void *arg)
{
    GstTIImgenc1* imgenc1    = GST_TIIMGENC1(gst_object_ref(arg));
    void*        threadRet = GstTIThreadSuccess;
    GstBuffer*   encData;
    Int          fifoRet;

    GST_LOG("Begin\n");
    while (TRUE) {

        /* Get the next input buffer (or block until one is ready) */
        fifoRet = Fifo_get(imgenc1->hInFifo, &encData);

        if (fifoRet < 0) {
            GST_ERROR("Failed to get buffer from input fifo\n");
            goto thread_failure;
        }

        /* Did the image thread flush the fifo? */
        if (fifoRet == Dmai_EFLUSH) {
            goto thread_exit;
        }

        /* Send the buffer to the circular buffer */
        if (!gst_ticircbuffer_queue_data(imgenc1->circBuf, encData)) {
            goto thread_failure;
        }

        /* Release the buffer we received from the sink pad */
        gst_buffer_unref(encData);

        /* If we've reached the EOS, start draining the circular buffer when
         * there are no more buffers in the FIFO.
         */
        if (imgenc1->drainingEOS && 
            Fifo_getNumEntries(imgenc1->hInFifo) == 0) {
            gst_ticircbuffer_drain(imgenc1->circBuf, TRUE);
        }

        /* Unblock any pending puts to our Fifo if we have reached our
         * minimum threshold.
         */
        gst_tiimgenc1_broadcast_queue_thread(imgenc1);
    }

thread_failure:
    gst_tithread_set_status(imgenc1, TIThread_QUEUE_ABORTED);
    threadRet = GstTIThreadFailure;

thread_exit:
    gst_object_unref(imgenc1);
    GST_LOG("Finish\n");
    return threadRet;
}


/******************************************************************************
 * gst_tiimgenc1_wait_on_queue_thread
 *    Wait for the queue thread to consume buffers from the fifo until
 *    there are only "waitQueueSize" items remaining.
 ******************************************************************************/
static void gst_tiimgenc1_wait_on_queue_thread(GstTIImgenc1 *imgenc1,
                Int32 waitQueueSize)
{
    GST_LOG("Begin\n");
    imgenc1->waitQueueSize = waitQueueSize;
    Rendezvous_meet(imgenc1->waitOnQueueThread);
    GST_LOG("Finish\n");
}


/******************************************************************************
 * gst_tiimgenc1_broadcast_queue_thread
 *    Broadcast when queue thread has processed enough buffers from the
 *    fifo to unblock anyone waiting to queue some more.
 ******************************************************************************/
static void gst_tiimgenc1_broadcast_queue_thread(GstTIImgenc1 *imgenc1)
{
    GST_LOG("Begin\n");
    if (imgenc1->waitQueueSize < Fifo_getNumEntries(imgenc1->hInFifo)) {
          return;
    } 
    Rendezvous_forceAndReset(imgenc1->waitOnQueueThread);
    GST_LOG("Finish\n");
}


/******************************************************************************
 * gst_tiimgenc1_drain_pipeline
 *    Push any remaining input buffers through the queue and encode threads
 ******************************************************************************/
static void gst_tiimgenc1_drain_pipeline(GstTIImgenc1 *imgenc1)
{
    gboolean checkResult;

    GST_LOG("Begin\n");
    imgenc1->drainingEOS = TRUE;

    /* If the processing threads haven't been created, there is nothing to
     * drain.
     */
    if (!gst_tithread_check_status(
             imgenc1, TIThread_DECODE_CREATED, checkResult)) {
        return;
    }
    if (!gst_tithread_check_status(
             imgenc1, TIThread_QUEUE_CREATED, checkResult)) {
        return;
    }

    /* If the queue fifo still has entries in it, it will drain the
     * circular buffer once all input buffers have been added to the
     * circular buffer.  If the fifo is already empty, we must drain
     * the circular buffer here.
     */
    if (Fifo_getNumEntries(imgenc1->hInFifo) == 0) {
        gst_ticircbuffer_drain(imgenc1->circBuf, TRUE);
    }
    else {
        Rendezvous_force(imgenc1->waitOnQueueThread);
    }

    /* Wait for the encoder to drain */
    if (!imgenc1->encodeDrained) {
        Rendezvous_meet(imgenc1->waitOnEncodeDrain);
    }
    imgenc1->encodeDrained = FALSE;

    GST_LOG("Finish\n");
}


/******************************************************************************
 * gst_tiimgenc1_frame_duration
 *    Return the duration of a single frame in nanoseconds.
 ******************************************************************************/
static GstClockTime gst_tiimgenc1_frame_duration(GstTIImgenc1 *imgenc1)
{
    GST_LOG("Begin\n");
    /* Default to 29.97 if the frame rate was not specified */
    if (imgenc1->framerateNum == 0 && imgenc1->framerateDen == 0) {
        GST_WARNING("framerate not specified; using 29.97fps");
        imgenc1->framerateNum = 30000;
        imgenc1->framerateDen = 1001;
    }

    GST_LOG("Finish\n");
    return (GstClockTime)
        ((1 / ((gdouble)imgenc1->framerateNum/(gdouble)imgenc1->framerateDen))
         * GST_SECOND);
}


/******************************************************************************
 * Custom ViM Settings for editing this file
 ******************************************************************************/
#if 0
 Tabs (use 4 spaces for indentation)
 vim:set tabstop=4:      /* Use 4 spaces for tabs          */
 vim:set shiftwidth=4:   /* Use 4 spaces for >> operations */
 vim:set expandtab:      /* Expand tabs into white spaces  */
#endif
