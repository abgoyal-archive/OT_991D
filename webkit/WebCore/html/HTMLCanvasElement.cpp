

#include "config.h"
#include "HTMLCanvasElement.h"

#include "CanvasContextAttributes.h"
#include "CanvasGradient.h"
#include "CanvasPattern.h"
#include "CanvasRenderingContext2D.h"
#if ENABLE(3D_CANVAS)    
#include "WebGLContextAttributes.h"
#include "WebGLRenderingContext.h"
#endif
#include "CanvasStyle.h"
#include "Chrome.h"
#include "Document.h"
#include "ExceptionCode.h"
#include "Frame.h"
#include "GraphicsContext.h"
#include "HTMLNames.h"
#include "ImageBuffer.h"
#include "MIMETypeRegistry.h"
#include "MappedAttribute.h"
#include "Page.h"
#include "RenderHTMLCanvas.h"
#include "Settings.h"
#include <math.h>
#include <stdio.h>

namespace WebCore {

using namespace HTMLNames;

// These values come from the WhatWG spec.
static const int defaultWidth = 300;
static const int defaultHeight = 150;

// Firefox limits width/height to 32767 pixels, but slows down dramatically before it 
// reaches that limit. We limit by area instead, giving us larger maximum dimensions,
// in exchange for a smaller maximum canvas size.
const float HTMLCanvasElement::MaxCanvasArea = 32768 * 8192; // Maximum canvas area in CSS pixels

HTMLCanvasElement::HTMLCanvasElement(const QualifiedName& tagName, Document* doc)
    : HTMLElement(tagName, doc)
    , m_size(defaultWidth, defaultHeight)
    , m_observer(0)
    , m_originClean(true)
    , m_ignoreReset(false)
    , m_createdImageBuffer(false)
{
    ASSERT(hasTagName(canvasTag));
}

HTMLCanvasElement::~HTMLCanvasElement()
{
    if (m_observer)
        m_observer->canvasDestroyed(this);
}

#if ENABLE(DASHBOARD_SUPPORT)

HTMLTagStatus HTMLCanvasElement::endTagRequirement() const 
{
    Settings* settings = document()->settings();
    if (settings && settings->usesDashboardBackwardCompatibilityMode())
        return TagStatusForbidden; 

    return HTMLElement::endTagRequirement();
}

int HTMLCanvasElement::tagPriority() const 
{ 
    Settings* settings = document()->settings();
    if (settings && settings->usesDashboardBackwardCompatibilityMode())
        return 0; 

    return HTMLElement::tagPriority();
}

#endif

void HTMLCanvasElement::parseMappedAttribute(MappedAttribute* attr)
{
    const QualifiedName& attrName = attr->name();
    if (attrName == widthAttr || attrName == heightAttr)
        reset();
    HTMLElement::parseMappedAttribute(attr);
}

RenderObject* HTMLCanvasElement::createRenderer(RenderArena* arena, RenderStyle* style)
{
    Settings* settings = document()->settings();
    if (settings && settings->isJavaScriptEnabled()) {
        m_rendererIsCanvas = true;
        return new (arena) RenderHTMLCanvas(this);
    }

    m_rendererIsCanvas = false;
    return HTMLElement::createRenderer(arena, style);
}

void HTMLCanvasElement::setHeight(int value)
{
    setAttribute(heightAttr, String::number(value));
}

void HTMLCanvasElement::setWidth(int value)
{
    setAttribute(widthAttr, String::number(value));
}

String HTMLCanvasElement::toDataURL(const String& mimeType, ExceptionCode& ec)
{
    if (!m_originClean) {
        ec = SECURITY_ERR;
        return String();
    }

    if (m_size.isEmpty() || !buffer())
        return String("data:,");

    if (mimeType.isNull() || !MIMETypeRegistry::isSupportedImageMIMETypeForEncoding(mimeType))
        return buffer()->toDataURL("image/png");

    return buffer()->toDataURL(mimeType);
}

CanvasRenderingContext* HTMLCanvasElement::getContext(const String& type, CanvasContextAttributes* attrs)
{
    // A Canvas can either be "2D" or "webgl" but never both. If you request a 2D canvas and the existing
    // context is already 2D, just return that. If the existing context is WebGL, then destroy it
    // before creating a new 2D context. Vice versa when requesting a WebGL canvas. Requesting a
    // context with any other type string will destroy any existing context.
    
    // FIXME - The code depends on the context not going away once created, to prevent JS from
    // seeing a dangling pointer. So for now we will disallow the context from being changed
    // once it is created.
    if (type == "2d") {
        if (m_context && !m_context->is2d())
            return 0;
        if (!m_context)
            m_context = new CanvasRenderingContext2D(this);
        return m_context.get();
    }
#if ENABLE(3D_CANVAS)    
    Settings* settings = document()->settings();
    if (settings && settings->webGLEnabled()) {
        // Accept the legacy "webkit-3d" name as well as the provisional "experimental-webgl" name.
        // Once ratified, we will also accept "webgl" as the context name.
        if ((type == "webkit-3d") ||
            (type == "experimental-webgl")) {
            if (m_context && !m_context->is3d())
                return 0;
            if (!m_context) {
                m_context = WebGLRenderingContext::create(this, static_cast<WebGLContextAttributes*>(attrs));
                if (m_context) {
                    // Need to make sure a RenderLayer and compositing layer get created for the Canvas
                    setNeedsStyleRecalc(SyntheticStyleChange);
                }
            }
            return m_context.get();
        }
    }
#else
    UNUSED_PARAM(attrs);
#endif
    return 0;
}

void HTMLCanvasElement::willDraw(const FloatRect& rect)
{
    if (m_imageBuffer)
        m_imageBuffer->clearImage();
    
    if (RenderBox* ro = renderBox()) {
        FloatRect destRect = ro->contentBoxRect();
        FloatRect r = mapRect(rect, FloatRect(0, 0, m_size.width(), m_size.height()), destRect);
        r.intersect(destRect);
        if (m_dirtyRect.contains(r))
            return;

        m_dirtyRect.unite(r);
        ro->repaintRectangle(enclosingIntRect(m_dirtyRect));
    }
    
    if (m_observer)
        m_observer->canvasChanged(this, rect);
}

void HTMLCanvasElement::reset()
{
    if (m_ignoreReset)
        return;

    bool ok;
    int w = getAttribute(widthAttr).toInt(&ok);
    if (!ok)
        w = defaultWidth;
    int h = getAttribute(heightAttr).toInt(&ok);
    if (!ok)
        h = defaultHeight;

    IntSize oldSize = m_size;
    m_size = IntSize(w, h);

#if ENABLE(3D_CANVAS)
    if (m_context && m_context->is3d())
        static_cast<WebGLRenderingContext*>(m_context.get())->reshape(width(), height());
#endif

    bool hadImageBuffer = m_createdImageBuffer;
    m_createdImageBuffer = false;
    m_imageBuffer.clear();
    if (m_context && m_context->is2d())
        static_cast<CanvasRenderingContext2D*>(m_context.get())->reset();

    if (RenderObject* renderer = this->renderer()) {
        if (m_rendererIsCanvas) {
            if (oldSize != m_size)
                toRenderHTMLCanvas(renderer)->canvasSizeChanged();
            if (hadImageBuffer)
                renderer->repaint();
        }
    }

    if (m_observer)
        m_observer->canvasResized(this);
}

void HTMLCanvasElement::paint(GraphicsContext* context, const IntRect& r)
{
    // Clear the dirty rect
    m_dirtyRect = FloatRect();

    if (context->paintingDisabled())
        return;
    
#if ENABLE(3D_CANVAS)
    WebGLRenderingContext* context3D = 0;
    if (m_context && m_context->is3d()) {
        context3D = static_cast<WebGLRenderingContext*>(m_context.get());
        context3D->beginPaint();
    }
#endif

    if (m_imageBuffer) {
        Image* image = m_imageBuffer->image();
        if (image)
            context->drawImage(image, DeviceColorSpace, r);
    }

#if ENABLE(3D_CANVAS)
    if (context3D)
        context3D->endPaint();
#endif
}

IntRect HTMLCanvasElement::convertLogicalToDevice(const FloatRect& logicalRect) const
{
    return IntRect(convertLogicalToDevice(logicalRect.location()), convertLogicalToDevice(logicalRect.size()));
}

IntSize HTMLCanvasElement::convertLogicalToDevice(const FloatSize& logicalSize) const
{
#if PLATFORM(ANDROID)
    /*  In Android we capture the drawing into a displayList, and then
        replay that list at various scale factors (sometimes zoomed out, other
        times zoomed in for "normal" reading, yet other times at arbitrary
        zoom values based on the user's choice). In all of these cases, we do
        not re-record the displayList, hence it is usually harmful to perform
        any pre-rounding, since we just don't know the actual drawing resolution
        at record time.
     */
    float pageScaleFactor = 1.0f;
#else
    float pageScaleFactor = document()->frame() ? document()->frame()->page()->chrome()->scaleFactor() : 1.0f;
#endif
    float wf = ceilf(logicalSize.width() * pageScaleFactor);
    float hf = ceilf(logicalSize.height() * pageScaleFactor);
    
    if (!(wf >= 1 && hf >= 1 && wf * hf <= MaxCanvasArea))
        return IntSize();

    return IntSize(static_cast<unsigned>(wf), static_cast<unsigned>(hf));
}

IntPoint HTMLCanvasElement::convertLogicalToDevice(const FloatPoint& logicalPos) const
{
#if PLATFORM(ANDROID)
    /*  In Android we capture the drawing into a displayList, and then
        replay that list at various scale factors (sometimes zoomed out, other
        times zoomed in for "normal" reading, yet other times at arbitrary
        zoom values based on the user's choice). In all of these cases, we do
        not re-record the displayList, hence it is usually harmful to perform
        any pre-rounding, since we just don't know the actual drawing resolution
        at record time.
     */
    float pageScaleFactor = 1.0f;
#else
    float pageScaleFactor = document()->frame() ? document()->frame()->page()->chrome()->scaleFactor() : 1.0f;
#endif
    float xf = logicalPos.x() * pageScaleFactor;
    float yf = logicalPos.y() * pageScaleFactor;
    
    return IntPoint(static_cast<unsigned>(xf), static_cast<unsigned>(yf));
}

void HTMLCanvasElement::createImageBuffer() const
{
    ASSERT(!m_imageBuffer);

    m_createdImageBuffer = true;
    
    FloatSize unscaledSize(width(), height());
    IntSize size = convertLogicalToDevice(unscaledSize);
    if (!size.width() || !size.height())
        return;

    m_imageBuffer = ImageBuffer::create(size);
    // The convertLogicalToDevice MaxCanvasArea check should prevent common cases
    // where ImageBuffer::create() returns NULL, however we could still be low on memory.
    if (!m_imageBuffer)
        return;
    m_imageBuffer->context()->scale(FloatSize(size.width() / unscaledSize.width(), size.height() / unscaledSize.height()));
    m_imageBuffer->context()->setShadowsIgnoreTransforms(true);
}

GraphicsContext* HTMLCanvasElement::drawingContext() const
{
    return buffer() ? m_imageBuffer->context() : 0;
}

ImageBuffer* HTMLCanvasElement::buffer() const
{
    if (!m_createdImageBuffer)
        createImageBuffer();
    return m_imageBuffer.get();
}
    
AffineTransform HTMLCanvasElement::baseTransform() const
{
    ASSERT(m_createdImageBuffer);
    FloatSize unscaledSize(width(), height());
    IntSize size = convertLogicalToDevice(unscaledSize);
    AffineTransform transform;
    if (size.width() && size.height())
        transform.scaleNonUniform(size.width() / unscaledSize.width(), size.height() / unscaledSize.height());
    transform.multiply(m_imageBuffer->baseTransform());
    return transform;
}

#if ENABLE(3D_CANVAS)    
bool HTMLCanvasElement::is3D() const
{
    return m_context && m_context->is3d();
}
#endif

}
