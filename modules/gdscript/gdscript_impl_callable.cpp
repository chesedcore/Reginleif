/**************************************************************************/
/*  gdscript_impl_callable.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

/// Hey! This file was created for the Reginleif fork! It's not part of Godot upstream.
/// Copyright (c) 2026 chesedcore (Monarch).
/// Licensed under the MIT License, same terms as the Godot engine.

#include "gdscript_impl_callable.h"

#include "gdscript_function.h"

#include "core/templates/hashfuncs.h"

bool GDScriptImplCallable::compare_equal(const CallableCustom* p_a, const CallableCustom* p_b) {
	return p_a == p_b;
}

bool GDScriptImplCallable::compare_less(const CallableCustom* p_a, const CallableCustom* p_b) {
	return p_a < p_b;
}

bool GDScriptImplCallable::is_valid() const {
	return function != nullptr;
}

uint32_t GDScriptImplCallable::hash() const {
	return h;
}

String GDScriptImplCallable::get_as_text() const {
	if (function == nullptr) {
		return "<Invalid ImplCallable>";
	}
	if (function->get_name() != StringName()) {
		return function->get_name().string() + "(ImplCallable)";
	}
	return "(anonymous ImplCallable)";
}

CallableCustom::CompareEqualFunc GDScriptImplCallable::get_compare_equal_func() const {
	return compare_equal;
}

CallableCustom::CompareLessFunc GDScriptImplCallable::get_compare_less_func() const {
	return compare_less;
}

ObjectID GDScriptImplCallable::get_object() const {
	// No backing Object for a builtin-type impl method; nothing to return.
	return ObjectID();
}

StringName GDScriptImplCallable::get_method() const {
	return function->get_name();
}

int GDScriptImplCallable::get_argument_count(bool& r_is_valid) const {
	if (function == nullptr) {
		r_is_valid = false;
		return 0;
	}
	r_is_valid = true;
	///`base` always occupies the implicit leading argument slot, same as the
	///`captures` subtraction in GDScriptLambdaCallable, but always exactly 1
    ///because an impl block can't capture lol
	return function->get_argument_count() - 1;
}

void GDScriptImplCallable::call(const Variant** p_arguments, int p_argcount, Variant& r_return_value, Callable::CallError& r_call_error) const {
	if (function == nullptr) {
		r_return_value = Variant();
		r_call_error.error = Callable::CallError::CALL_ERROR_INSTANCE_IS_NULL;
		return;
	}

	///`base` is supposed to be stuffed into the callable as the implicit leading argument, 
    ///matching the exact convention OPCODE_CALL already uses for native_impl_function 
    ///(base, then real call args, if you forgot)
	const int total_argcount = p_argcount + 1;
	const Variant** args = (const Variant**)alloca(sizeof(Variant*)* total_argcount);
	args[0] = &base;
	for (int i = 0; i < p_argcount; i++) {
		args[i + 1] = p_arguments[i];
	}

	r_return_value = function->call(nullptr, args, total_argcount, r_call_error);
	switch (r_call_error.error) {
		case Callable::CallError::CALL_ERROR_INVALID_ARGUMENT:
			r_call_error.argument -= 1;
#ifdef DEBUG_ENABLED
			if (r_call_error.argument < 0) {
				ERR_PRINT("Reginleif bug (please report!): Invalid value of bound impl base argument.");
				r_call_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
				r_call_error.argument = 0;
				r_call_error.expected = 0;
			}
#endif
			break;
		case Callable::CallError::CALL_ERROR_TOO_MANY_ARGUMENTS:
		case Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS:
			r_call_error.expected -= 1;
#ifdef DEBUG_ENABLED
			if (r_call_error.expected < 0) {
				ERR_PRINT("Reginleif bug (please report!): Invalid bound impl base argument count.");
				r_call_error.error = Callable::CallError::CALL_ERROR_INVALID_METHOD;
				r_call_error.argument = 0;
				r_call_error.expected = 0;
			}
#endif
			break;
		default:
			break;
	}
}

GDScriptImplCallable::GDScriptImplCallable(const Variant& p_base, GDScriptFunction* p_function) :
		function(p_function) {
	ERR_FAIL_NULL(p_function);
	base = p_base;

	h = (uint32_t)hash_murmur3_one_64((uint64_t)this);
}