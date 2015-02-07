

#include "config.h"
#include "WebKitDLL.h"
#include "WebError.h"
#include "WebKit.h"

#pragma warning(push, 0)
#include <WebCore/BString.h>
#pragma warning(pop)

#if USE(CFNETWORK)
#include <WebKitSystemInterface/WebKitSystemInterface.h>
#endif

using namespace WebCore;

// WebError ---------------------------------------------------------------------

WebError::WebError(const ResourceError& error, IPropertyBag* userInfo)
    : m_refCount(0)
    , m_error(error)
    , m_userInfo(userInfo)
{
    gClassCount++;
    gClassNameCount.add("WebError");
}

WebError::~WebError()
{
    gClassCount--;
    gClassNameCount.remove("WebError");
}

WebError* WebError::createInstance(const ResourceError& error, IPropertyBag* userInfo)
{
    WebError* instance = new WebError(error, userInfo);
    instance->AddRef();
    return instance;
}

WebError* WebError::createInstance()
{
    return createInstance(ResourceError());
}

// IUnknown -------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE WebError::QueryInterface(REFIID riid, void** ppvObject)
{
    *ppvObject = 0;
    if (IsEqualGUID(riid, IID_IUnknown))
        *ppvObject = static_cast<IWebError*>(this);
    else if (IsEqualGUID(riid, CLSID_WebError))
        *ppvObject = static_cast<WebError*>(this);
    else if (IsEqualGUID(riid, IID_IWebError))
        *ppvObject = static_cast<IWebError*>(this);
    else if (IsEqualGUID(riid, IID_IWebErrorPrivate))
        *ppvObject = static_cast<IWebErrorPrivate*>(this);
    else
        return E_NOINTERFACE;

    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE WebError::AddRef(void)
{
    return ++m_refCount;
}

ULONG STDMETHODCALLTYPE WebError::Release(void)
{
    ULONG newRef = --m_refCount;
    if (!newRef)
        delete(this);

    return newRef;
}

// IWebError ------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE WebError::init( 
    /* [in] */ BSTR domain,
    /* [in] */ int code,
    /* [in] */ BSTR url)
{
    m_error = ResourceError(String(domain, SysStringLen(domain)), code, String(url, SysStringLen(url)), String());
    return S_OK;
}
  
HRESULT STDMETHODCALLTYPE WebError::code( 
    /* [retval][out] */ int* result)
{
    *result = m_error.errorCode();
    return S_OK;
}
        
HRESULT STDMETHODCALLTYPE WebError::domain( 
    /* [retval][out] */ BSTR* result)
{
    if (!result)
        return E_POINTER;

    *result = BString(m_error.domain()).release();
    return S_OK;
}
               
HRESULT STDMETHODCALLTYPE WebError::localizedDescription( 
    /* [retval][out] */ BSTR* result)
{
    if (!result)
        return E_POINTER;

    *result = BString(m_error.localizedDescription()).release();

#if USE(CFNETWORK)
    if (!*result) {
        if (int code = m_error.errorCode())
            *result = BString(wkCFNetworkErrorGetLocalizedDescription(code)).release();
    }
#endif

    return S_OK;
}

        
HRESULT STDMETHODCALLTYPE WebError::localizedFailureReason( 
    /* [retval][out] */ BSTR* /*result*/)
{
    ASSERT_NOT_REACHED();
    return E_NOTIMPL;
}
        
HRESULT STDMETHODCALLTYPE WebError::localizedRecoveryOptions( 
    /* [retval][out] */ IEnumVARIANT** /*result*/)
{
    ASSERT_NOT_REACHED();
    return E_NOTIMPL;
}
        
HRESULT STDMETHODCALLTYPE WebError::localizedRecoverySuggestion( 
    /* [retval][out] */ BSTR* /*result*/)
{
    ASSERT_NOT_REACHED();
    return E_NOTIMPL;
}
       
HRESULT STDMETHODCALLTYPE WebError::recoverAttempter( 
    /* [retval][out] */ IUnknown** /*result*/)
{
    ASSERT_NOT_REACHED();
    return E_NOTIMPL;
}
        
HRESULT STDMETHODCALLTYPE WebError::userInfo( 
    /* [retval][out] */ IPropertyBag** result)
{
    if (!result)
        return E_POINTER;
    *result = 0;

    if (!m_userInfo)
        return E_FAIL;

    return m_userInfo.copyRefTo(result);
}

HRESULT STDMETHODCALLTYPE WebError::failingURL( 
    /* [retval][out] */ BSTR* result)
{
    if (!result)
        return E_POINTER;

    *result = BString(m_error.failingURL()).release();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WebError::isPolicyChangeError( 
    /* [retval][out] */ BOOL *result)
{
    if (!result)
        return E_POINTER;

    *result = m_error.domain() == String(WebKitErrorDomain)
        && m_error.errorCode() == WebKitErrorFrameLoadInterruptedByPolicyChange;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WebError::sslPeerCertificate( 
    /* [retval][out] */ OLE_HANDLE* result)
{
    if (!result)
        return E_POINTER;
    *result = 0;

#if USE(CFNETWORK)
    if (!m_cfErrorUserInfoDict) {
        // copy userinfo from CFErrorRef
        CFErrorRef cfError = m_error;
        m_cfErrorUserInfoDict.adoptCF(CFErrorCopyUserInfo(cfError));
    }

    if (!m_cfErrorUserInfoDict)
        return E_FAIL;

    void* data = wkGetSSLPeerCertificateData(m_cfErrorUserInfoDict.get());
    if (!data)
        return E_FAIL;
    *result = (OLE_HANDLE)(ULONG64)data;
#endif
    return *result ? S_OK : E_FAIL;
}

const ResourceError& WebError::resourceError() const
{
    return m_error;
}
