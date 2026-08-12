/**************************************************************************/
/*  gdscript_impl_callable.h                                              */
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

#pragma once

/// Hey! This file was created for the Reginleif fork! It's not part of Godot upstream.
/// Copyright (c) 2026 chesedcore (Monarch).
/// Licensed under the MIT License, same terms as the Godot engine.

#include "core/variant/callable.h"
#include "core/variant/variant.h"

class GDScriptFunction;

///callable that binds a base Variant value (int, String, Vector2, your mother, etc) as the
///implicit leading argument to an `impl` method, so that bare attribute access like
///`69.some_method_that_i_implemented_a_trait_for` produces a usable callable.
///was created to be wired up for the builtin-type native impl registry, 
///but not tied to that specifically, any random fuckass (base, GDScriptFunction*) pair works.
///see GDScriptLambdaCallable for the pattern this is modeled on
///"but monarch!", i hear you say, "cant you just reuse an existing callable implementation?"
///o young padawan, if only you took a gander at the VM, direct method calls work because i write
///directly to the fucking stack. however when you need an actual callable (the function suddenly
///has a refcounted lifetime?!) you need something to capture the implicit base, which is why i made this
class GDScriptImplCallable : public CallableCustom {
	GDScriptFunction *function = nullptr;
	Variant base; ///the native/builtin piggybacking off this callable
	uint32_t h;

	static bool compare_equal(const CallableCustom* p_a, const CallableCustom* p_b);
	static bool compare_less(const CallableCustom* p_a, const CallableCustom* p_b);

public:
	bool is_valid() const override;
	uint32_t hash() const override;
	String get_as_text() const override;
	CompareEqualFunc get_compare_equal_func() const override;
	CompareLessFunc get_compare_less_func() const override;
	ObjectID get_object() const override;
	StringName get_method() const override;
	int get_argument_count(bool &r_is_valid) const override;
	void call(const Variant** p_arguments, int p_argcount, Variant& r_return_value, Callable::CallError& r_call_error) const override;

	GDScriptImplCallable(GDScriptImplCallable&) = delete;
	GDScriptImplCallable(const GDScriptImplCallable&) = delete;
	GDScriptImplCallable(const Variant& p_base, GDScriptFunction* p_function);
	virtual ~GDScriptImplCallable() = default;
};