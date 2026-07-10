/**************************************************************************/
/*  gdscript_trait_analyzer.cpp                                           */
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

#include "gdscript_trait_analyzer.h"
#include "gdscript_analyzer.h"
#include "core/object/class_db.h"

GDScriptTraitAnalyzer::GDScriptTraitAnalyzer(GDScriptParser* p_parser, GDScriptAnalyzer* p_analyzer) {
	parser = p_parser;
	analyzer = p_analyzer;
}

void GDScriptTraitAnalyzer::push_error(const String& p_message, const GDScriptParser::Node* p_source) {
	parser->push_error(p_message, p_source);
}

Error GDScriptTraitAnalyzer::resolve_trait(GDScriptParser::TraitNode* p_trait) {
	ERR_FAIL_NULL_V(p_trait, ERR_INVALID_PARAMETER);

	const StringName trait_name = p_trait->identifier != nullptr ? p_trait->identifier->name : StringName();

	if (trait_name == StringName()) {
		push_error(R"([Reginleif] Trait declaration is missing a name.)", p_trait);
		return ERR_PARSE_ERROR;
	}

	if (resolved_traits.has(trait_name)) {
		///already resolved, bail the fuck out
		return OK;
	}

	Ref<GDScriptTrait> gd_trait;
	gd_trait.instantiate();
	gd_trait->name = trait_name;
	gd_trait->script_path = parser->get_script_path();
	gd_trait->trait_node = p_trait;

	///stash these before resolving method signatures, in case a default method body
	///some-fucking-how ends up referencing the trait itself
	///classes do this too!! i love gdscript!!! yay!!!
	resolved_traits[trait_name] = gd_trait;

	for (GDScriptParser::FunctionNode* method : p_trait->methods) {
		if (method == nullptr || method->identifier == nullptr) {
			continue;
		}
		analyzer->resolve_function_signature(method, p_trait);
		gd_trait->required_methods[method->identifier->name] = method;
	}

	for (GDScriptParser::FunctionNode* method : p_trait->default_methods) {
		if (method == nullptr || method->identifier == nullptr) {
			continue;
		}
		analyzer->resolve_function_signature(method, p_trait);
		analyzer->resolve_function_body(method);
		gd_trait->default_methods[method->identifier->name] = method;
	}

	///resolve `impl <this trait> for Type` (and `impl for Type` shorthand) blocks that
	///reside in this same trait file, like something implementing the trait directly on a built-in
	///!!!NOTE:!!! these targets are usually BUILTIN/NATIVE, NOT a fucking ClassNode*, so `check_trait_satisfaction()`
	///(which only ever gets called from `resolve_class_body()`) never sees them. check coverage here instead!!
	bool impls_ok = true;
	for (GDScriptParser::ImplNode* impl : p_trait->impls) {
		if (impl == nullptr) {
			continue;
		}

		if (resolve_impl(impl) != OK) {
			impls_ok = false;
			continue;
		}

		///`resolve_impl()` just pushed this onto `resolved_impls` (hopefully :sob:), so fetch it back to check coverage
		Ref<GDScriptImpl> gd_impl = resolved_impls.size() > 0 ? resolved_impls.back()->get() : Ref<GDScriptImpl>();
		if (gd_impl.is_null() || gd_impl->impl_node != impl) {
			continue; /// well fuck me if it ever happens, but if it does, keep moving as if 
					  /// nothing went wrong :>
		}

		for (const KeyValue<StringName, GDScriptParser::FunctionNode*>& E : gd_trait->required_methods) {
			if (!gd_impl->provided_methods.has(E.key)) {
				push_error(vformat(R"([Reginleif] "impl" for trait "%s" does not implement required method "%s".)", trait_name, E.key), impl);
				impls_ok = false;
			}
		}
	}

	if (!impls_ok) {
		return ERR_PARSE_ERROR;
	}

	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

Error GDScriptTraitAnalyzer::resolve_impl(GDScriptParser::ImplNode* p_impl) {
	ERR_FAIL_NULL_V(p_impl, ERR_INVALID_PARAMETER);

	StringName trait_name;

	if (p_impl->trait_name != nullptr) {
		trait_name = p_impl->trait_name->name;
	} else {
		///`impl for Type` shorthand
		///!!!only!!! legal inside the trait file that owns the trait being shortcut to
		if (!parser->is_trait_script() || parser->get_trait_tree() == nullptr || parser->get_trait_tree()->identifier == nullptr) {
			push_error(R"([Reginleif] "impl for Type" without a trait name is only allowed inside that trait's own file.)", p_impl);
			return ERR_PARSE_ERROR;
		}

		trait_name = parser->get_trait_tree()->identifier->name;
	}

	///!!TODO:!! fall back to cross-file lookup once that exists... maybe. 
	///for now, only local traits are usable...
	Ref<GDScriptTrait> trait = get_local_trait(trait_name);
	if (trait.is_null()) {
		push_error(vformat(R"([Reginleif] Could not find trait "%s".)", trait_name), p_impl);
		return ERR_PARSE_ERROR;
	}

	GDScriptParser::DataType target_type;
	if (p_impl->impl_target_type != nullptr) {
		///`impl Trait for Type`
		target_type = analyzer->type_from_metatype(analyzer->resolve_datatype(p_impl->impl_target_type));
	} else if (parser->current_class != nullptr) {
		///in-class `impl Trait` sugar, so target is whatever class we're scanning inside
		target_type = parser->current_class->get_datatype();
	} else {
		push_error(R"([Reginleif] "impl" block has no target type and is not inside a class.)", p_impl);
		return ERR_PARSE_ERROR;
	}

	if (!check_orphan_rule(trait, target_type, p_impl)) {
		push_error(vformat(R"([Reginleif] Cannot "impl" trait "%s" for this type: neither the trait nor the target type is declared in this file.)", trait_name), p_impl);
		return ERR_PARSE_ERROR;
	}

	Ref<GDScriptImpl> gd_impl;
	gd_impl.instantiate();
	gd_impl->trait = trait;
	gd_impl->impl_target_type = target_type;
	gd_impl->impl_node = p_impl;
	gd_impl->trait_owns_this_impl = p_impl->trait_owns_this_impl;

	bool ok = true;

	for (GDScriptParser::FunctionNode* provided : p_impl->methods) {
		if (provided == nullptr || provided->identifier == nullptr) {
			continue;
		}

		const StringName method_name = provided->identifier->name;
		analyzer->resolve_function_signature(provided, p_impl);

		if (!trait->has_method(method_name)) {
			push_error(vformat(R"([Reginleif] Trait "%s" has no method "%s" to implement.)", trait_name, method_name), provided);
			ok = false;
			continue;
		}

		GDScriptParser::FunctionNode* required = trait->required_methods.has(method_name) ?
				trait->required_methods[method_name] :
				trait->default_methods[method_name];

		if (!_signatures_match(required, provided)) {
			push_error(vformat(R"([Reginleif] Method "%s" does not match the signature required by trait "%s".)", method_name, trait_name), provided);
			ok = false;
			continue;
		}

		analyzer->resolve_function_body(provided);

		gd_impl->provided_methods[method_name] = provided;
	}

	///Anything not explicitly provided falls back to the trait's own default body(if it has one)
	for (const KeyValue<StringName, GDScriptParser::FunctionNode*>& E : trait->default_methods) {
		if (!gd_impl->provided_methods.has(E.key)) {
			gd_impl->provided_methods[E.key] = E.value;
		}
	}

	resolved_impls.push_back(gd_impl);

	return ok ? OK : ERR_PARSE_ERROR;
}

Error GDScriptTraitAnalyzer::check_trait_satisfaction(GDScriptParser::ClassNode* p_class) {
	ERR_FAIL_NULL_V(p_class, ERR_INVALID_PARAMETER);

	bool ok = true;

	///!!!NOTE:!! only considers impls resolved so far in *this* file! children inheriting a parent's
	///impls, and cross-file impls, aren't handled until cross-file trait lookup exists!!
	for (const Ref<GDScriptImpl>& impl : resolved_impls) {
		if (impl.is_null() || impl->trait.is_null()) {
			continue;
		}

		GDScriptParser::ClassNode* target_class = impl->get_target_class();
		if (target_class != p_class) {
			continue;
		}

		const GDScriptParser::Node* error_source = impl->impl_node != nullptr ? static_cast<const GDScriptParser::Node*>(impl->impl_node) : static_cast<const GDScriptParser::Node*>(p_class);

		for (const KeyValue<StringName, GDScriptParser::FunctionNode*>& E : impl->trait->required_methods) {
			if (!impl->provided_methods.has(E.key)) {
				push_error(vformat(R"([Reginleif] Class "%s" does not implement required method "%s" of trait "%s".)",
						p_class->identifier != nullptr ? String(p_class->identifier->name) : p_class->fqcn,
						E.key,
						impl->trait->name),
						error_source);
				ok = false;
			}
		}
	}

	return ok ? OK : ERR_PARSE_ERROR;
}

bool GDScriptTraitAnalyzer::check_orphan_rule(const Ref<GDScriptTrait>& p_trait, const GDScriptParser::DataType& p_target_type, const GDScriptParser::Node* p_source) {
	if (p_trait.is_valid() && resolved_traits.has(p_trait->name)) {
		return true;
	}

	///we don't own the trait atp, so we can only impl it if we own the target
	///BUILTIN/NATIVE/SCRIPT targets can never be owned by us
	///only a CLASS declared in this file counts for ownership by us
	if (p_target_type.kind != GDScriptParser::DataType::CLASS) {
		return false;
	}

	return p_target_type.class_type != nullptr && parser->has_class(p_target_type.class_type);
}

Ref<GDScriptTrait> GDScriptTraitAnalyzer::get_local_trait(const StringName& p_name) const {
	if (resolved_traits.has(p_name)) {
		return resolved_traits[p_name];
	}
	return Ref<GDScriptTrait>();
}

bool GDScriptTraitAnalyzer::_signatures_match(GDScriptParser::FunctionNode* p_required, GDScriptParser::FunctionNode* p_provided) {
	if (p_required == nullptr || p_provided == nullptr) {
		return false;
	}

	if (p_required->parameters.size() != p_provided->parameters.size()) {
		return false;
	}

	for (int i = 0; i < p_required->parameters.size(); i++) {
		GDScriptParser::DataType required_param_type = p_required->parameters[i]->get_datatype();
		GDScriptParser::DataType provided_param_type = p_provided->parameters[i]->get_datatype();

		///parameters are contravariant in general, but gdscript doesn't do variance gymnastics
		///anywhere else either, so we just require them to line up both ways (effectively equal)
		///if you find this fucky, so do i, congrats!
		if (!analyzer->is_type_compatible(required_param_type, provided_param_type, false) ||
				!analyzer->is_type_compatible(provided_param_type, required_param_type, false)) {
			return false;
		}
	}

	GDScriptParser::DataType required_return = p_required->get_datatype();
	GDScriptParser::DataType provided_return = p_provided->get_datatype();

	///don't allow a Variant return to slip through when a required return is specified
	if (!required_return.is_variant() && provided_return.is_variant()) {
		return false;
	}

	///return type IS allowed to be covariant! providing something more specific than required is fine.
	if (!analyzer->is_type_compatible(required_return, provided_return, true)) {
		return false;
	}

	return true;
}

bool GDScriptTraitAnalyzer::_type_is_or_inherits(const GDScriptParser::DataType& p_type, const GDScriptParser::DataType& p_target_type) const {
	if (p_type == p_target_type) {
		return true;
	}

	if (p_target_type.kind == GDScriptParser::DataType::CLASS) {
		///walk up the CLASS inheritance chain looking for p_target_type's class...
		GDScriptParser::ClassNode* current = p_type.kind == GDScriptParser::DataType::CLASS ? p_type.class_type : nullptr;
		while (current != nullptr) {
			if (current == p_target_type.class_type) {
				return true;
			}
			current = current->base_type.kind == GDScriptParser::DataType::CLASS ? current->base_type.class_type : nullptr;
		}
		return false;
	}

	if (p_target_type.kind == GDScriptParser::DataType::NATIVE && p_type.native_type != StringName()) {
		return ClassDB::is_parent_class(p_type.native_type, p_target_type.native_type);
	}

	return false;
}

bool GDScriptTraitAnalyzer::type_satisfies_trait(const GDScriptParser::DataType& p_concrete_type, const Ref<GDScriptTrait>& p_trait) const {
	if (p_trait.is_null()) {
		return false;
	}

	///!!!NOTE:!!! cross-file impls not yet considered, per the 9000 billion comments
	///i've already made :>
	for (const Ref<GDScriptImpl>& impl : resolved_impls) {
		if (impl.is_null() || impl->trait != p_trait) {
			continue;
		}

		if (_type_is_or_inherits(p_concrete_type, impl->impl_target_type)) {
			return true;
		}
	}

	return false;
}