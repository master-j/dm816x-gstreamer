/*
 * gsttiprepencbuf.c
 *
 * This file defines the "TIPrepEncBuf" element, which prepares a GstBuffer
 * for use with the encoders used by the TIVidenc1 element.  Typically this
 * means copying the buffer into a physically contiguous buffer, and performing
 * a color conversion on platforms like DM6467.  Hardware acceleration is used
 * for a copy.
 *
 * Platforms where TIVidenc1 supports zero-copy encode (such as DM365) should
 * not use this element when performing capture+encode.  In these cases, the
 * encoder can process buffers directly from MMAP capture buffers.
 *
 * Original Author:
 *     Don Darling, Texas Instruments, Inc.
 *
 * Example usage:
 *     gst-launch v4l2src ! TIPrepEncBuf ! queue ! TIVidenc1 ! ...
 *
 * Note: Buffers of type TIDMAIBUFFERTRANSPORT are pushed to the source pad.
 * Downstream elements can detect this and obtain the DMAI buffers for use
 * with hardware-accelerated operations.
 *
 * Copyright (C) 2008-2010 Texas Instruments Incorporated - http://www.ti.com/
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

#include <gst/gst.h>
#include <gst/video/video.h>

#include <ti/sdo/dmai/BufferGfx.h>

#include "gsttiprepencbuf.h"
#include "gsttidmaibuffertransport.h"
#include "gstticommonutils.h"

/* Declare variable used to categorize GST_LOG output */
GST_DEBUG_CATEGORY_STATIC (gst_tiprepencbuf_debug);
#define GST_CAT_DEFAULT gst_tiprepencbuf_debug

/* Element properties */
enum {
  PROP_0,
  PROP_CONTIG_INPUT_FRAME,  /*  contiguousInputFrame (boolean) */
  PROP_NUM_OUTPUT_BUFS,     /*  numOutputBufs        (gint)    */
};

/* Define property default */
#define DEFAULT_NUM_OUTPUT_BUFS         2
#define DEFAULT_CONTIGUOUS_INPUT_FRAME  FALSE
#define gst_tiprepencbuf_invalid_device Cpu_Device_COUNT

/* Define static caps for the sink and src pads */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ( GST_VIDEO_CAPS_YUV("UYVY")";"
      GST_VIDEO_CAPS_YUV("NV16")";"
      GST_VIDEO_CAPS_YUV("Y8C8")";"
      GST_VIDEO_CAPS_YUV("NV12")
    )
);

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE(
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ( GST_VIDEO_CAPS_YUV("UYVY")";"
      GST_VIDEO_CAPS_YUV("NV16")";"
      GST_VIDEO_CAPS_YUV("Y8C8")";"
      GST_VIDEO_CAPS_YUV("NV12")
    )
);

/* Declare a global pointer to our element base class */
static GstElementClass *parent_class = NULL;

/* Static Function Declarations */
static void
  gst_tiprepencbuf_init(GstTIPrepEncBuf *object);
static void
  gst_tiprepencbuf_base_init(gpointer gclass);
static void
  gst_tiprepencbuf_class_init(GstTIPrepEncBufClass *g_class);
static GstFlowReturn
  gst_tiprepencbuf_prepare_output_buffer (GstBaseTransform *trans,
    GstBuffer *inBuf, gint size, GstCaps *caps, GstBuffer **outBuf);
static void
  gst_tiprepencbuf_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec);
static gboolean
  gst_tiprepencbuf_transform_size (GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, guint size, GstCaps *othercaps,
    guint *othersize);
static Buffer_Handle
  gst_tiprepencbuf_convert_gst_to_dmai(GstTIPrepEncBuf *prepencbuf,
    GstBuffer *buf, gboolean reference);
static Int
  gst_tiprepencbuf_422psemi_420psemi(Buffer_Handle hDstBuf, GstBuffer *src, 
    GstTIPrepEncBuf *prepencbuf);
static Int
  gst_tiprepencbuf_copy_input(GstTIPrepEncBuf *prepencbuf,
    Buffer_Handle hDstBuf, GstBuffer *src);
static GstFlowReturn
  gst_tiprepencbuf_transform (GstBaseTransform *trans, GstBuffer *inBuf,
    GstBuffer *outBuf);
static GstCaps*
  gst_tiprepencbuf_transform_caps (GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps);
static gboolean
  gst_tiprepencbuf_parse_caps (GstCaps *cap, gint *width, gint *height,
    guint32 *fourcc);
static ColorSpace_Type
  gst_tiprepencbuf_get_colorSpace (guint32 fourcc);
static gboolean
  gst_tiprepencbuf_set_caps (GstBaseTransform *trans, GstCaps *in,
    GstCaps *out);
static gboolean
  gst_tiprepencbuf_exit(GstTIPrepEncBuf *prepencbuf);

/******************************************************************************
 * gst_tiprepencbuf_get_type
 *    Boiler-plate function auto-generated by "make_element" script.
 *    Defines function pointers for initialization routines for this element.
 ******************************************************************************/
GType gst_tiprepencbuf_get_type(void)
{
    static GType object_type = 0;

    if (G_UNLIKELY(object_type == 0)) {
        static const GTypeInfo object_info = {
            sizeof(GstTIPrepEncBufClass),
            gst_tiprepencbuf_base_init,
            NULL,
            (GClassInitFunc) gst_tiprepencbuf_class_init,
            NULL,
            NULL,
            sizeof(GstTIPrepEncBuf),
            0,
            (GInstanceInitFunc) gst_tiprepencbuf_init
        };

        object_type = g_type_register_static(GST_TYPE_BASE_TRANSFORM,
            "GstTIPrepEncBuf", &object_info, (GTypeFlags) 0);

        /* Initialize GST_LOG for this object */
        GST_DEBUG_CATEGORY_INIT(gst_tiprepencbuf_debug, "TIPrepEncBuf",
                                0, "Prepare GstBuffer for Encoding");

        GST_LOG("initialized get_type\n");
    }

    return object_type;
}

/******************************************************************************
 * gst_tiprepencbuf_init
 *****************************************************************************/
static void gst_tiprepencbuf_init(GstTIPrepEncBuf * prepencbuf)
{
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(prepencbuf), TRUE);

    prepencbuf->contiguousInputFrame = DEFAULT_CONTIGUOUS_INPUT_FRAME;
    prepencbuf->numOutputBufs        = DEFAULT_NUM_OUTPUT_BUFS;
    prepencbuf->hFc                  = NULL;

    /* Determine target board type */
    if (Cpu_getDevice(NULL, &prepencbuf->device) < 0) {
        GST_ELEMENT_ERROR(prepencbuf, RESOURCE, FAILED,
            ("Failed to determine target board\n"), (NULL));
        prepencbuf->device = gst_tiprepencbuf_invalid_device;
    }

}

/******************************************************************************
 * gst_tiprepencbuf_base_init
 *    Boiler-plate function auto-generated by "make_element" script.
 *    Initializes element base class.
 ******************************************************************************/
static void gst_tiprepencbuf_base_init(gpointer gclass)
{
    static GstElementDetails element_details = {
        "TI Physically Contiguous Buffer",
        "Filter/Copy",
        "Copy buffer into physically contigous buffer",
        "Don Darling; Texas Instruments, Inc."
    };

    GstElementClass *element_class = GST_ELEMENT_CLASS(gclass);

    gst_element_class_add_pad_template(element_class,
        gst_static_pad_template_get(&src_factory));
    gst_element_class_add_pad_template(element_class,
        gst_static_pad_template_get(&sink_factory));
    gst_element_class_set_details(element_class, &element_details);
}

/******************************************************************************
 * gst_tiprepencbuf_class_init
 *    Boiler-plate function auto-generated by "make_element" script.
 *    Initializes the TIPrepEncBuf class.
 ******************************************************************************/
static void gst_tiprepencbuf_class_init(GstTIPrepEncBufClass * klass)
{
    GObjectClass          *gobject_class;
    GstBaseTransformClass *trans_class;

    parent_class  = g_type_class_peek_parent(klass);
    gobject_class = (GObjectClass *) klass;
    trans_class   = (GstBaseTransformClass *) klass;

    gobject_class->set_property = gst_tiprepencbuf_set_property;
    gobject_class->finalize     = (GObjectFinalizeFunc) gst_tiprepencbuf_exit;

    trans_class->passthrough_on_same_caps = FALSE;

    trans_class->set_caps =
        GST_DEBUG_FUNCPTR(gst_tiprepencbuf_set_caps);
    trans_class->transform_caps =
        GST_DEBUG_FUNCPTR(gst_tiprepencbuf_transform_caps);
    trans_class->transform_size =
        GST_DEBUG_FUNCPTR(gst_tiprepencbuf_transform_size);
    trans_class->transform =
        GST_DEBUG_FUNCPTR(gst_tiprepencbuf_transform);
    trans_class->prepare_output_buffer =
        GST_DEBUG_FUNCPTR(gst_tiprepencbuf_prepare_output_buffer);

    g_object_class_install_property(gobject_class, PROP_CONTIG_INPUT_FRAME,
        g_param_spec_boolean("contiguousInputFrame", "Contiguous input buffer",
            "Set this if element recieves contiguous input frame from"
            " upstream.", DEFAULT_CONTIGUOUS_INPUT_FRAME, G_PARAM_WRITABLE));

    g_object_class_install_property(gobject_class, PROP_NUM_OUTPUT_BUFS,
        g_param_spec_int("numOutputBufs", "Number of Output buffers",
            "Number of output buffers to allocate", 1, G_MAXINT32,
            DEFAULT_NUM_OUTPUT_BUFS, G_PARAM_WRITABLE));

    GST_LOG("initialized class init\n");
}

/*****************************************************************************
 * gst_tiprepencbuf_prepare_output_buffer
 *    Function is used to allocate output buffer
 *****************************************************************************/
static GstFlowReturn
gst_tiprepencbuf_prepare_output_buffer(GstBaseTransform * trans,
    GstBuffer * inBuf, gint size, GstCaps * caps, GstBuffer ** outBuf)
{
    GstTIPrepEncBuf *prepencbuf = GST_TIPREPENCBUF(trans);
    Buffer_Handle    hOutBuf;

    GST_LOG("begin prepare output buffer\n");

    /* Get free buffer from buftab */
    if (!(hOutBuf = gst_tidmaibuftab_get_buf(prepencbuf->hOutBufTab))) {
        GST_ELEMENT_ERROR(prepencbuf, RESOURCE, READ,
            ("failed to get free buffer\n"), (NULL));
        return GST_FLOW_ERROR;
    }

    /* Create a DMAI transport buffer object to carry a DMAI buffer to
     * the source pad.  The transport buffer knows how to release the
     * buffer for re-use in this element when the source pad calls
     * gst_buffer_unref().
     */
    GST_LOG("creating dmai transport buffer\n");
    *outBuf = gst_tidmaibuffertransport_new(hOutBuf, prepencbuf->hOutBufTab);
    gst_buffer_set_data(*outBuf, (guint8 *) Buffer_getUserPtr(hOutBuf),
        Buffer_getSize(hOutBuf));
    gst_buffer_set_caps(*outBuf, GST_PAD_CAPS(trans->srcpad));

    GST_LOG("end prepare output buffer\n");

    return GST_FLOW_OK;
}

/******************************************************************************
 * gst_tividenc_set_property
 *     Set element properties when requested.
 ******************************************************************************/
static void
gst_tiprepencbuf_set_property(GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
    GstTIPrepEncBuf *prepencbuf = GST_TIPREPENCBUF(object);

    GST_LOG("begin set_property\n");

    switch (prop_id) {
        case PROP_CONTIG_INPUT_FRAME:
            prepencbuf->contiguousInputFrame = g_value_get_boolean(value);
            GST_LOG("setting \"contiguousInputFrame\" to \"%s\"\n",
                prepencbuf->contiguousInputFrame ? "TRUE" : "FALSE");
            break;
        case PROP_NUM_OUTPUT_BUFS:
            prepencbuf->numOutputBufs = g_value_get_int(value);
            GST_LOG("setting \"numOutputBufs\" to \"%d\"\n",
                prepencbuf->numOutputBufs);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }

    GST_LOG("end set_property\n");
}
       
/******************************************************************************
 * gst_tiprepencbuf_transform_size
 *   The output buffer will always be the same size as the input buffer
 *****************************************************************************/ 
static gboolean
gst_tiprepencbuf_transform_size(GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, guint size, GstCaps * othercaps,
    guint * othersize)
{
    GstTIPrepEncBuf *prepencbuf = GST_TIPREPENCBUF(trans);

    switch (direction) {
        case GST_PAD_SINK:
            *othersize = gst_ti_calc_buffer_size(prepencbuf->dstWidth,
                prepencbuf->dstHeight, 0, prepencbuf->dstColorSpace);
            break;
        case GST_PAD_SRC:
            *othersize = gst_ti_calc_buffer_size(prepencbuf->srcWidth,
                prepencbuf->srcHeight, 0, prepencbuf->srcColorSpace);
            break;
        case GST_PAD_UNKNOWN:
            return FALSE;
    }

    return TRUE;
}

/******************************************************************************
 * gst_tiprepencbuf_convert_gst_to_dmai
 *  This function convert gstreamer buffer into DMAI graphics buffer.
 *****************************************************************************/
static Buffer_Handle
gst_tiprepencbuf_convert_gst_to_dmai(GstTIPrepEncBuf * prepencbuf,
    GstBuffer * buf, gboolean reference)
{
    BufferGfx_Attrs gfxAttrs = BufferGfx_Attrs_DEFAULT;
    Buffer_Handle   hBuf     = NULL;

    gfxAttrs.bAttrs.reference = reference;
    gfxAttrs.dim.width        = prepencbuf->srcWidth;
    gfxAttrs.dim.height       = prepencbuf->srcHeight;
    gfxAttrs.colorSpace       = prepencbuf->srcColorSpace;
    gfxAttrs.dim.lineLength   = BufferGfx_calcLineLength(gfxAttrs.dim.width,
                                    prepencbuf->srcColorSpace);

    hBuf = Buffer_create(GST_BUFFER_SIZE(buf),
        BufferGfx_getBufferAttrs(&gfxAttrs));

    if (hBuf == NULL) {
        GST_ERROR("failed to create  buffer\n");
        return NULL;
    }
    Buffer_setUserPtr(hBuf, (Int8 *) GST_BUFFER_DATA(buf));
    Buffer_setNumBytesUsed(hBuf, GST_BUFFER_SIZE(buf));
    return hBuf;
}

/******************************************************************************
 * gst_tiprepencbuf_422psemi_420psemi
 *  this function color convert YUV422PSEMI to YUV420PSEMI.
 *****************************************************************************/
static Int
gst_tiprepencbuf_422psemi_420psemi(Buffer_Handle hDstBuf, GstBuffer * src,
    GstTIPrepEncBuf * prepencbuf)
{
    Ccv_Attrs     ccvAttrs = Ccv_Attrs_DEFAULT;
    gboolean      accel    = FALSE;
    Buffer_Handle hInBuf   = NULL;
    Int           ret      = -1;

    GST_LOG("gst_tiprepencbuf_422psemi_420psemi - begin\n");

    /* create ccv handle */
    if (prepencbuf->hCcv == NULL) {
        /* Enable the accel ccv based on contiguousInputFrame.
         * If accel is set to FALSE then DMAI will use software ccv function
         * else will use HW accelerated ccv engine.
         */

        /* If we are getting dmai transport buffer then enable HW 
         * acceleration */
        if (GST_IS_TIDMAIBUFFERTRANSPORT(src)) {
            accel = TRUE;
        }
        else {
            accel = prepencbuf->contiguousInputFrame;
        }

        ccvAttrs.accel = prepencbuf->contiguousInputFrame;
        prepencbuf->hCcv = Ccv_create(&ccvAttrs);
        if (prepencbuf->hCcv == NULL) {
            GST_ERROR("failed to create CCV handle\n");
            goto exit;
        }

        GST_INFO("HW accel CCV: %s\n", accel ? "enabled" : "disabled");
    }

    /* Prepare input buffer */
    hInBuf = gst_tiprepencbuf_convert_gst_to_dmai(prepencbuf, src, TRUE);
    if (hInBuf == NULL) {
        GST_ERROR("failed to get dmai buffer\n");
        goto exit;
    }

    /* Prepare output buffer */
    if (Ccv_config(prepencbuf->hCcv, hInBuf, hDstBuf) < 0) {
        GST_ERROR("failed to config CCV handle\n");
        goto exit;
    }

    if (Ccv_execute(prepencbuf->hCcv, hInBuf, hDstBuf) < 0) {
        GST_ERROR("failed to execute Ccv handle\n");
        goto exit;
    }

    ret = GST_BUFFER_SIZE(src);

    GST_LOG("gst_tiprepencbuf_422psemi_420psemi - end\n");

exit:
    if (hInBuf) {
        Buffer_delete(hInBuf);
    }

    return ret;
}

/*****************************************************************************
 * gst_tiprepencbuf_copy_input
 *  Make the input data in src available in the physically contiguous memory
 *  in dst in the best way possible.  Preferably an accelerated copy or
 *  color conversion.
 ****************************************************************************/
static Int
gst_tiprepencbuf_copy_input(GstTIPrepEncBuf * prepencbuf,
    Buffer_Handle hDstBuf, GstBuffer * src)
{
    Framecopy_Attrs  fcAttrs = Framecopy_Attrs_DEFAULT;
    gboolean         accel   = FALSE;
    Buffer_Handle    hInBuf  = NULL;
    Int              ret     = -1;

#if defined(Platform_dm365)
    BufferGfx_Dimensions dim;
#endif

    /* Check to see if we need to execute ccv on dm6467 */
    if (prepencbuf->device == Cpu_Device_DM6467 &&
        prepencbuf->srcColorSpace == ColorSpace_YUV422PSEMI) {
        return gst_tiprepencbuf_422psemi_420psemi(hDstBuf, src, prepencbuf);
    }

    GST_LOG("gst_tiphyscontig_copy_input - begin\n");

    if (prepencbuf->hFc == NULL) {
        /* Enable the accel framecopy based on contiguousInputFrame.
         * If accel is set to FALSE then DMAI will use regular memcpy function
         * else will use HW accelerated framecopy.
         */

        /* If we are getting dmai transport buffer then enable HW 
         * acceleration */
        if (GST_IS_TIDMAIBUFFERTRANSPORT(src)) {
            accel = TRUE;
        }
        else {
            accel = prepencbuf->contiguousInputFrame;
        }

        fcAttrs.accel = prepencbuf->contiguousInputFrame;

        prepencbuf->hFc = Framecopy_create(&fcAttrs);
        if (prepencbuf->hFc == NULL) {
            GST_ERROR("failed to create framecopy handle\n");
            goto exit;
        }

        GST_INFO("HW accel framecopy: %s\n", accel ? "enabled" : "disabled");
    }

    /* Prepare input buffer */
    hInBuf = gst_tiprepencbuf_convert_gst_to_dmai(prepencbuf, src, TRUE);
    if (hInBuf == NULL) {
        GST_ERROR("failed to get dmai buffer\n");
        goto exit;
    }

#if defined(Platform_dm365)
    /* Handle resizer 32-byte issue on DM365 platform */
    if (prepencbuf->device == Cpu_Device_DM365) {
        if ((prepencbuf->srcColorSpace == ColorSpace_YUV420PSEMI)) {
            BufferGfx_getDimensions(hInBuf, &dim);
            dim.lineLength = Dmai_roundUp(dim.lineLength, 32);
            BufferGfx_setDimensions(hInBuf, &dim);
        }
    }
#endif

    /* Prepare output buffer */
    if (Framecopy_config(prepencbuf->hFc, hInBuf, hDstBuf) < 0) {
        GST_ERROR("failed to configure framecopy\n");
        goto exit;
    }

    if (Framecopy_execute(prepencbuf->hFc, hInBuf, hDstBuf) < 0) {
        GST_ERROR("failed to execute framecopy\n");
        goto exit;
    }

    ret = GST_BUFFER_SIZE(src);

exit:
    if (hInBuf) {
        Buffer_delete(hInBuf);
    }

    GST_LOG("gst_tiprepencbuf_copy_input - end\n");
    return ret;
}

/******************************************************************************
 * gst_tiprepencbuf_transform 
 *    Transforms one incoming buffer to one outgoing buffer.
 *****************************************************************************/
static GstFlowReturn
gst_tiprepencbuf_transform(GstBaseTransform * trans, GstBuffer * src,
    GstBuffer * dst)
{
    GstTIPrepEncBuf *prepencbuf = GST_TIPREPENCBUF(trans);

    /* If the input buffer is a physically contiguous DMAI buffer, it can
     * be passed directly to the codec.
     */
    if (GST_IS_TIDMAIBUFFERTRANSPORT(src)) {
        gst_buffer_unref(dst);
        dst = gst_buffer_ref(src);
        return GST_FLOW_OK;
    }

    /* Otherwise, copy the buffer contents into our local physically contiguous
     * DMAI buffer.  The gst_tiprepencbuf_copy_input function will copy
     * using hardware acceleration if possible.
     */
    if (!(gst_tiprepencbuf_copy_input(prepencbuf,
        GST_TIDMAIBUFFERTRANSPORT_DMAIBUF(dst), src))) {
        return GST_FLOW_ERROR;
    }
    return GST_FLOW_OK;
}

/******************************************************************************
 * gst_tiprepencbuf_transform_caps
 *   Most platforms require that the exact same caps are used on both ends.
 *   DM6467 can also convert from Y8C8/NV16 to NV12.
 *****************************************************************************/
static GstCaps*
gst_tiprepencbuf_transform_caps(GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
    GstTIPrepEncBuf *prepencbuf = GST_TIPREPENCBUF(trans);
    GstCaps         *supported_caps;
    GstStructure    *caps_struct;

    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    /* We always support the same caps on both sides */
    supported_caps = gst_caps_copy(caps);

    /* On DM6467, we also support conversion from Y8C8/NV16 to NV12 */
    if (prepencbuf->device == Cpu_Device_DM6467) {
        switch (direction) {
            case GST_PAD_SINK:
                caps_struct =
                    gst_structure_copy(gst_caps_get_structure(caps, 0));
                gst_structure_set(caps_struct, "format", GST_TYPE_FOURCC,
                    GST_MAKE_FOURCC('N', 'V', '1', '2'), (gchar *) NULL);
                gst_caps_append_structure(supported_caps, caps_struct);
                break;
            case GST_PAD_SRC:
                caps_struct =
                    gst_structure_copy(gst_caps_get_structure(caps, 0));
                gst_structure_set(caps_struct, "format", GST_TYPE_FOURCC,
                    GST_MAKE_FOURCC('N', 'V', '1', '6'), (gchar *) NULL);
                gst_caps_append_structure(supported_caps, caps_struct);
                break;
            case GST_PAD_UNKNOWN:
                break;
        }
    }

    return supported_caps;
}

/******************************************************************************
 * gst_tiprepencbuf_parse_caps
 *****************************************************************************/
static gboolean
gst_tiprepencbuf_parse_caps(GstCaps * cap, gint * width, gint * height,
    guint32 * format)
{
    GstStructure *structure = gst_caps_get_structure(cap, 0);

    GST_LOG("begin parse caps\n");

    if (!gst_structure_get_int(structure, "width", width)) {
        GST_ERROR("Failed to get width \n");
        return FALSE;
    }

    if (!gst_structure_get_int(structure, "height", height)) {
        GST_ERROR("Failed to get height \n");
        return FALSE;
    }

    if (!gst_structure_get_fourcc(structure, "format", format)) {
        GST_ERROR("failed to get fourcc from cap\n");
        return FALSE;
    }

    GST_LOG("end parse caps\n");

    return TRUE;
}

/*****************************************************************************
 * gst_tiprepencbuf_get_colorSpace
 ****************************************************************************/
ColorSpace_Type gst_tiprepencbuf_get_colorSpace(guint32 fourcc)
{
    switch (fourcc) {
        case GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'):
            return ColorSpace_UYVY;
        case GST_MAKE_FOURCC('N', 'V', '1', '6'):
        case GST_MAKE_FOURCC('Y', '8', 'C', '8'):
            return ColorSpace_YUV422PSEMI;
        case GST_MAKE_FOURCC('N', 'V', '1', '2'):
            return ColorSpace_YUV420PSEMI;
        default:
            GST_ERROR("failed to get colorspace\n");
            return ColorSpace_NOTSET;
    }
}

/******************************************************************************
 * gst_tiprepencbuf_set_caps
 *****************************************************************************/
static gboolean
gst_tiprepencbuf_set_caps(GstBaseTransform * trans, GstCaps * in,
    GstCaps * out)
{
    GstTIPrepEncBuf *prepencbuf = GST_TIPREPENCBUF(trans);
    BufferGfx_Attrs  gfxAttrs   = BufferGfx_Attrs_DEFAULT;
    gboolean         ret        = FALSE;
    guint32          fourcc;
    guint            outBufSize;

    GST_LOG("begin set caps\n");

    /* parse input cap */
    if (!gst_tiprepencbuf_parse_caps(in, &prepencbuf->srcWidth,
        &prepencbuf->srcHeight, &fourcc)) {
        GST_ELEMENT_ERROR(prepencbuf, RESOURCE, FAILED,
                          ("failed to get input resolution\n"), (NULL));
        goto exit;
    }

    GST_LOG("input fourcc %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS(fourcc));

    /* map fourcc with its corresponding dmai colorspace type */
    prepencbuf->srcColorSpace = gst_tiprepencbuf_get_colorSpace(fourcc);

    /* parse output cap */
    if (!gst_tiprepencbuf_parse_caps(out, &prepencbuf->dstWidth,
        &prepencbuf->dstHeight, &fourcc)) {
        GST_ELEMENT_ERROR(prepencbuf, RESOURCE, FAILED,
                          ("failed to get output resolution\n"), (NULL));
        goto exit;
    }

    GST_LOG("output fourcc %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS(fourcc));

    /* map fourcc with its corresponding dmai colorspace type */
    prepencbuf->dstColorSpace = gst_tiprepencbuf_get_colorSpace(fourcc);

    /* calculate output buffer size */
    outBufSize = gst_ti_calc_buffer_size(prepencbuf->dstWidth,
                     prepencbuf->dstHeight, 0, prepencbuf->dstColorSpace);

    /* allocate output buffer */
    gfxAttrs.bAttrs.useMask = gst_tidmaibuffer_GST_FREE;
    gfxAttrs.colorSpace     = prepencbuf->dstColorSpace;
    gfxAttrs.dim.width      = prepencbuf->dstWidth;
    gfxAttrs.dim.height     = prepencbuf->dstHeight;
    gfxAttrs.dim.lineLength = 
        BufferGfx_calcLineLength(gfxAttrs.dim.width, gfxAttrs.colorSpace);

    if (prepencbuf->numOutputBufs == 0) {
        prepencbuf->numOutputBufs = 2;
    }

    prepencbuf->hOutBufTab = gst_tidmaibuftab_new(prepencbuf->numOutputBufs,
        outBufSize, BufferGfx_getBufferAttrs (&gfxAttrs));

    if (prepencbuf->hOutBufTab == NULL) {
        GST_ELEMENT_ERROR(prepencbuf, RESOURCE, NO_SPACE_LEFT,
            ("failed to create output bufTab\n"), (NULL));
        goto exit;
    }

    ret = TRUE;

exit:
    GST_LOG("end set caps\n");
    return ret;
}

/******************************************************************************
 * gst_tiprepencbuf_exit
 *    Shut down any running framecopy, and reset the element state.
 ******************************************************************************/
static gboolean gst_tiprepencbuf_exit(GstTIPrepEncBuf * prepencbuf)
{
    GST_LOG("begin exit_video\n");

    /* Shut down remaining items */
    if (prepencbuf->hCcv) {
        GST_LOG("deleting Ccv instance\n");
        Ccv_delete(prepencbuf->hCcv);
        prepencbuf->hCcv = NULL;
    }

    if (prepencbuf->hFc) {
        GST_LOG("deleting framecopy instance\n");
        Framecopy_delete(prepencbuf->hFc);
        prepencbuf->hFc = NULL;
    }

    if (prepencbuf->hOutBufTab) {
        GST_LOG("freeing output buffers\n");
        gst_tidmaibuftab_unref(prepencbuf->hOutBufTab);
        prepencbuf->hOutBufTab = NULL;
    }

    GST_LOG("end exit_video\n");
    return TRUE;
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
