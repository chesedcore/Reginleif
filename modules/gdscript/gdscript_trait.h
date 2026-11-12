/**************************************************************************/
/*  gdscript_trait.h                                                      */
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

#pragma once

#include "gdscript_parser.h"

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"


struct GDScriptDetachedType {
	GDScriptParser::DataType base;
	String class_path;
	StringName class_fqcn;
	String generic_owner_class_path;
	StringName generic_owner_class_fqcn;
	StringName generic_owner_function_name;
	Vector<GDScriptDetachedType> elements; ///container_element_types
	HashMap<StringName, GDScriptDetachedType> bindings; ///generic_type_bindings

	Vector<GDScriptDetachedType> callable_param_types;
	Vector<GDScriptDetachedType> callable_return_types;

	static GDScriptDetachedType detach(const GDScriptParser::DataType& p_type);
	GDScriptParser::DataType resolve(GDScriptParser* p_current) const;
};

class GDScriptTraitSignatureSnapshot : public RefCounted {
	GDSOFTCLASS(GDScriptTraitSignatureSnapshot, RefCounted);

public:
	Vector<StringName> param_names;
	int default_arg_count = 0;
	bool is_vararg = false;
	bool is_static = false;

	Vector<GDScriptDetachedType> param_detached;
	GDScriptDetachedType return_detached;

	Vector<GDScriptParser::DataType> get_param_types(GDScriptParser* p_current) const;
	GDScriptParser::DataType get_return_type(GDScriptParser* p_current) const;
};

/// compiler internal representation of a resolved `trait` declaration
class GDScriptTrait : public RefCounted {
	GDSOFTCLASS(GDScriptTrait, RefCounted);

public:
	StringName name;
	String script_path;
	GDScriptParser::TraitNode* trait_node = nullptr;
	Ref<GDScriptParserRef> parser_ref;

	HashMap<StringName, GDScriptParser::FunctionNode*> required_methods;
	HashMap<StringName, GDScriptParser::FunctionNode*> default_methods;

	/// snapshot of each required/default method's signature, taken at resolution time.
	/// because apparently FunctionNode* pointers just fuck off and dangle across separate
	/// GDScriptTraitAnalyzer instances, sometimes, for reasons i gave up trying to fully pin down
	/// (two parser trees for the same file?? in THIS economy??). so no, we don't trust
	/// ->parameters/->get_datatype() off the live node anymore. we trust THIS. always THIS.
	/// this is the way. this is the only way. i'm too tired to find the other way
	/// i'm just a college kid. i've got a test tomorrow. i can't be assed. the borrow checker
	/// doesn't exist to save me. help me. save me.
	HashMap<StringName, Ref<GDScriptTraitSignatureSnapshot>> required_signatures;

	_FORCE_INLINE_ bool has_method(const StringName& p_name) const {
		return required_methods.has(p_name) || default_methods.has(p_name);
	}

	_FORCE_INLINE_ bool is_required(const StringName& p_name) const {
		return required_methods.has(p_name);
	}
};


/// compiler-internal representation of a single resolved `impl for Type`
/// or in-class `impl Trait` block.
/// do note that only one instance of this is generated per impl block encountered
class GDScriptImpl : public RefCounted {
	GDSOFTCLASS(GDScriptImpl, RefCounted);

public:
	Ref<GDScriptTrait> trait;

    ///because i need to be able to target ANY type in the engine
	GDScriptParser::DataType impl_target_type;

	_FORCE_INLINE_ GDScriptParser::ClassNode* get_target_class() const {
		return impl_target_type.kind == GDScriptParser::DataType::CLASS ? 
                                        impl_target_type.class_type : 
                                        nullptr;
	}

	HashMap<StringName, GDScriptParser::FunctionNode*> provided_methods;
	HashMap<StringName, Ref<GDScriptTraitSignatureSnapshot>> provided_signatures;

	GDScriptParser::ImplNode* impl_node = nullptr;

	/// true for `impl Trait for Type` if it appears in this trait's own file
	bool trait_owns_this_impl = false;
};