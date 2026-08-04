/**************************************************************************/
/*  gdscript_trait_analyzer.h                                             */
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
#include "gdscript_trait.h"

#include "core/templates/list.h"

class GDScriptAnalyzer;

/// handles semantic analysis for `trait`/`impl` declarations
class GDScriptTraitAnalyzer {
	GDScriptParser* parser = nullptr;
	GDScriptAnalyzer* analyzer = nullptr;

	///!!!!NOTE:!!!!!! as of 2026-07-15
	///:NEVER: fucking deref FunctionNode* from `default_methods/required_methods` for
	///signature info (parameters, get_datatype, whatever the fuck)... 
	///those pointers can dangle across GDScriptTraitAnalyzer instances/reparses
	///Always use the MethodSignatureSnapshot in required_signatures instead!!!!!!!
	///why does this happen? there's a phantom parser/analyser pair being created
	///which i suspect MIGHT be from the code editor performing simultaneous analysis
	///and thus fucking up the static cache, resulting in use-after-frees!!!!!
	///i'll fix it when i can be assed to, i'm too tired at the moment of writing this

	/// traits declared in *this* file's parser tree, keyed by name.
	HashMap<StringName, Ref<GDScriptTrait>> resolved_traits;

	/// every impl block resolved in this file, in declaration order.
	List<Ref<GDScriptImpl>> resolved_impls;

private:
    void push_error(const String& p_message, const GDScriptParser::Node* p_source = nullptr);

    /// true iff p_provided's signature (params + return type) satisfies p_required
    /// p_required is the trait's method signature, p_provided is what an impl/class actually gives us
    bool _signatures_match(const Ref<GDScriptTraitSignatureSnapshot>& p_required, GDScriptParser::FunctionNode* p_provided);

    bool _type_is_or_inherits(const GDScriptParser::DataType& p_type, const GDScriptParser::DataType& p_target_type) const;
	StringName _impl_target_key(const GDScriptParser::DataType& p_type) const;
	String _owning_path_for_type(const GDScriptParser::DataType& p_type) const;

public:
	Error resolve_trait(GDScriptParser::TraitNode* p_trait);
	Error resolve_impl(GDScriptParser::ImplNode* p_impl);

	Error check_trait_satisfaction(GDScriptParser::ClassNode* p_class);

	bool check_orphan_rule(const Ref<GDScriptTrait>& p_trait, const GDScriptParser::DataType& p_target_type, const GDScriptParser::Node* p_source);

	Ref<GDScriptTrait> get_local_trait(const StringName& p_name) const;

	bool type_satisfies_trait(const GDScriptParser::DataType& p_concrete_type, const Ref<GDScriptTrait>& p_trait) const;
	bool global_claims_satisfy_trait(const GDScriptParser::DataType& p_concrete_type, const StringName& p_trait_name) const;

	const List<Ref<GDScriptImpl>>& get_resolved_impls() const { return resolved_impls; }
	_FORCE_INLINE_ GDScriptParser* get_parser() { return parser; }

	GDScriptParser::FunctionNode* find_impl_method(const GDScriptParser::DataType& p_base_type, const StringName& p_method_name) const;

	GDScriptTraitAnalyzer(GDScriptParser* p_parser, GDScriptAnalyzer* p_analyzer);
};