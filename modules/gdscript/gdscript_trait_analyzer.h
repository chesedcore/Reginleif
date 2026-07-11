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

	/// traits declared in *this* file's parser tree, keyed by name.
	HashMap<StringName, Ref<GDScriptTrait>> resolved_traits;

	/// every impl block resolved in this file, in declaration order.
	List<Ref<GDScriptImpl>> resolved_impls;

private:
    void push_error(const String& p_message, const GDScriptParser::Node* p_source = nullptr);

    /// true iff p_provided's signature (params + return type) satisfies p_required
    /// p_required is the trait's method signature, p_provided is what an impl/class actually gives us
    bool _signatures_match(GDScriptParser::FunctionNode* p_required, GDScriptParser::FunctionNode* p_provided);

    /// walks a NATIVE/CLASS/SCRIPT p_type's ancestor chain looking for a match against p_target_type
    /// used by check_orphan_rule (see implementation for more about this) and type_satisfies_trait (ask yourself, 
	/// "does the concrete type inherit from whatever an impl block targeted?")
    bool _type_is_or_inherits(const GDScriptParser::DataType& p_type, const GDScriptParser::DataType& p_target_type) const;
	StringName _impl_target_key(const GDScriptParser::DataType& p_type) const;
	String _owning_path_for_type(const GDScriptParser::DataType& p_type) const;

public:
	Error resolve_trait(GDScriptParser::TraitNode* p_trait);
	Error resolve_impl(GDScriptParser::ImplNode* p_impl);

	/// walks all impls targeting p_class 
    /// (children inherit parent impls, explicit re-impl overrides) and
	/// verifies required_methods are satisfied
	Error check_trait_satisfaction(GDScriptParser::ClassNode* p_class);

	/// the oprhan rule! true iff this file owns p_trait (i.e. declares it) or
	/// owns the CLASS-kind target (parser->has_class(...)). BUILTIN,
	/// NATIVE, and SCRIPT targets can never be "owned" by a script, so those
	/// impls are only legal when this file owns the trait.
	bool check_orphan_rule(const Ref<GDScriptTrait>& p_trait, const GDScriptParser::DataType& p_target_type, const GDScriptParser::Node* p_source);

	/// lookup within this file's resolved_traits map. returns a null Ref if
	/// not found locally (caller should fall back to cross-file lookup once
	/// that exists :sob:)
	Ref<GDScriptTrait> get_local_trait(const StringName& p_name) const;

	/// checks whether p_concrete_type satisfies p_trait, by scanning
	/// resolved_impls for one whose impl_target_type matches p_concrete_type
	/// (or an ancestor of it, for NATIVE/CLASS kinds). used for generic
	/// upper-bound checks (`T impl Positionable`) and `impl Trait` parameter
	/// sugar. cross-file impls not yet considered.
	bool type_satisfies_trait(const GDScriptParser::DataType& p_concrete_type, const Ref<GDScriptTrait>& p_trait) const;

	const List<Ref<GDScriptImpl>>& get_resolved_impls() const { return resolved_impls; }

	GDScriptParser::FunctionNode* find_impl_method(const GDScriptParser::DataType& p_base_type, const StringName& p_method_name) const;

	GDScriptTraitAnalyzer(GDScriptParser* p_parser, GDScriptAnalyzer* p_analyzer);
};