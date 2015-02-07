

#include "config.h"
#include "JSHistoryCustom.h"

#include "Frame.h"
#include "History.h"
#include <runtime/JSFunction.h>
#include <runtime/PrototypeFunction.h>

using namespace JSC;

namespace WebCore {

static JSValue nonCachingStaticBackFunctionGetter(ExecState* exec, const Identifier& propertyName, const PropertySlot&)
{
    return new (exec) NativeFunctionWrapper(exec, exec->lexicalGlobalObject()->prototypeFunctionStructure(), 0, propertyName, jsHistoryPrototypeFunctionBack);
}

static JSValue nonCachingStaticForwardFunctionGetter(ExecState* exec, const Identifier& propertyName, const PropertySlot&)
{
    return new (exec) NativeFunctionWrapper(exec, exec->lexicalGlobalObject()->prototypeFunctionStructure(), 0, propertyName, jsHistoryPrototypeFunctionForward);
}

static JSValue nonCachingStaticGoFunctionGetter(ExecState* exec, const Identifier& propertyName, const PropertySlot&)
{
    return new (exec) NativeFunctionWrapper(exec, exec->lexicalGlobalObject()->prototypeFunctionStructure(), 1, propertyName, jsHistoryPrototypeFunctionGo);
}

bool JSHistory::getOwnPropertySlotDelegate(ExecState* exec, const Identifier& propertyName, PropertySlot& slot)
{
    // When accessing History cross-domain, functions are always the native built-in ones.
    // See JSDOMWindow::getOwnPropertySlotDelegate for additional details.

    // Our custom code is only needed to implement the Window cross-domain scheme, so if access is
    // allowed, return false so the normal lookup will take place.
    String message;
    if (allowsAccessFromFrame(exec, impl()->frame(), message))
        return false;

    // Check for the few functions that we allow, even when called cross-domain.
    const HashEntry* entry = JSHistoryPrototype::s_info.propHashTable(exec)->entry(exec, propertyName);
    if (entry) {
        // Allow access to back(), forward() and go() from any frame.
        if (entry->attributes() & Function) {
            if (entry->function() == jsHistoryPrototypeFunctionBack) {
                slot.setCustom(this, nonCachingStaticBackFunctionGetter);
                return true;
            } else if (entry->function() == jsHistoryPrototypeFunctionForward) {
                slot.setCustom(this, nonCachingStaticForwardFunctionGetter);
                return true;
            } else if (entry->function() == jsHistoryPrototypeFunctionGo) {
                slot.setCustom(this, nonCachingStaticGoFunctionGetter);
                return true;
            }
        }
    } else {
        // Allow access to toString() cross-domain, but always Object.toString.
        if (propertyName == exec->propertyNames().toString) {
            slot.setCustom(this, objectToStringFunctionGetter);
            return true;
        }
    }

    printErrorMessageForFrame(impl()->frame(), message);
    slot.setUndefined();
    return true;
}

bool JSHistory::getOwnPropertyDescriptorDelegate(ExecState* exec, const Identifier& propertyName, PropertyDescriptor& descriptor)
{
    if (!impl()->frame()) {
        descriptor.setUndefined();
        return true;
    }

    // Throw out all cross domain access
    if (!allowsAccessFromFrame(exec, impl()->frame()))
        return true;

    // Check for the few functions that we allow, even when called cross-domain.
    const HashEntry* entry = JSHistoryPrototype::s_info.propHashTable(exec)->entry(exec, propertyName);
    if (entry) {
        PropertySlot slot;
        // Allow access to back(), forward() and go() from any frame.
        if (entry->attributes() & Function) {
            if (entry->function() == jsHistoryPrototypeFunctionBack) {
                slot.setCustom(this, nonCachingStaticBackFunctionGetter);
                descriptor.setDescriptor(slot.getValue(exec, propertyName), entry->attributes());
                return true;
            } else if (entry->function() == jsHistoryPrototypeFunctionForward) {
                slot.setCustom(this, nonCachingStaticForwardFunctionGetter);
                descriptor.setDescriptor(slot.getValue(exec, propertyName), entry->attributes());
                return true;
            } else if (entry->function() == jsHistoryPrototypeFunctionGo) {
                slot.setCustom(this, nonCachingStaticGoFunctionGetter);
                descriptor.setDescriptor(slot.getValue(exec, propertyName), entry->attributes());
                return true;
            }
        }
    } else {
        // Allow access to toString() cross-domain, but always Object.toString.
        if (propertyName == exec->propertyNames().toString) {
            PropertySlot slot;
            slot.setCustom(this, objectToStringFunctionGetter);
            descriptor.setDescriptor(slot.getValue(exec, propertyName), entry->attributes());
            return true;
        }
    }

    descriptor.setUndefined();
    return true;
}

bool JSHistory::putDelegate(ExecState* exec, const Identifier&, JSValue, PutPropertySlot&)
{
    // Only allow putting by frames in the same origin.
    if (!allowsAccessFromFrame(exec, impl()->frame()))
        return true;
    return false;
}

bool JSHistory::deleteProperty(ExecState* exec, const Identifier& propertyName)
{
    // Only allow deleting by frames in the same origin.
    if (!allowsAccessFromFrame(exec, impl()->frame()))
        return false;
    return Base::deleteProperty(exec, propertyName);
}

void JSHistory::getOwnPropertyNames(ExecState* exec, PropertyNameArray& propertyNames, EnumerationMode mode)
{
    // Only allow the history object to enumerated by frames in the same origin.
    if (!allowsAccessFromFrame(exec, impl()->frame()))
        return;
    Base::getOwnPropertyNames(exec, propertyNames, mode);
}

JSValue JSHistory::pushState(ExecState* exec, const ArgList& args)
{
    RefPtr<SerializedScriptValue> historyState = SerializedScriptValue::create(exec, args.at(0));
    if (exec->hadException())
        return jsUndefined();

    String title = valueToStringWithUndefinedOrNullCheck(exec, args.at(1));
    if (exec->hadException())
        return jsUndefined();
        
    String url;
    if (args.size() > 2) {
        url = valueToStringWithUndefinedOrNullCheck(exec, args.at(2));
        if (exec->hadException())
            return jsUndefined();
    }

    ExceptionCode ec = 0;
    impl()->stateObjectAdded(historyState.release(), title, url, History::StateObjectPush, ec);
    setDOMException(exec, ec);

    return jsUndefined();
}

JSValue JSHistory::replaceState(ExecState* exec, const ArgList& args)
{
    RefPtr<SerializedScriptValue> historyState = SerializedScriptValue::create(exec, args.at(0));
    if (exec->hadException())
        return jsUndefined();

    String title = valueToStringWithUndefinedOrNullCheck(exec, args.at(1));
    if (exec->hadException())
        return jsUndefined();
        
    String url;
    if (args.size() > 2) {
        url = valueToStringWithUndefinedOrNullCheck(exec, args.at(2));
        if (exec->hadException())
            return jsUndefined();
    }

    ExceptionCode ec = 0;
    impl()->stateObjectAdded(historyState.release(), title, url, History::StateObjectReplace, ec);
    setDOMException(exec, ec);

    return jsUndefined();
}

} // namespace WebCore
