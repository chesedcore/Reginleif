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

static Ref<GDScriptTraitSignatureSnapshot> _make_trait_method_signature_snapshot(const GDScriptParser::FunctionNode* p_method) {
	Ref<GDScriptTraitSignatureSnapshot> snapshot;
	snapshot.instantiate();
	if (p_method == nullptr) {
		return snapshot;
	}
	for (int i = 0; i < p_method->parameters.size(); i++) {
		snapshot->param_types.push_back(p_method->parameters[i]->type_constraint);
		snapshot->param_names.push_back(p_method->parameters[i]->identifier != nullptr ? p_method->parameters[i]->identifier->name : StringName());
		if (p_method->parameters[i]->initializer != nullptr) {
			snapshot->default_arg_count++;
		}
	}
	snapshot->return_type = p_method->return_type_constraint;
	snapshot->is_vararg = p_method->is_vararg();
	return snapshot;
}

GDScriptTraitAnalyzer::GDScriptTraitAnalyzer(GDScriptParser* p_parser, GDScriptAnalyzer* p_analyzer) {
	parser = p_parser;
	analyzer = p_analyzer;
	///wipe old claims before this pass adds fresh ones
	///otherwise reanalysis just piles up duplicates forever :<
	GDScriptCache::remove_global_impls_by_path(parser->get_script_path());
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

	Error detected_symbol_conflicts = analyzer->check_symbol_name_conflicts(trait_name, p_trait->identifier, parser->script_path);
	if (detected_symbol_conflicts != OK) {
		return ERR_PARSE_ERROR;
	}

	if (ScriptServer::is_global_class(trait_name) && !GDScript::is_canonically_equal_paths(ScriptServer::get_global_class_path(trait_name), parser->script_path)) {
		push_error(vformat(R"([Reginleif] Trait "%s" hides a global script class.)", trait_name), p_trait->identifier);
		return ERR_PARSE_ERROR;
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

		gd_trait->required_signatures[method->identifier->name] = _make_trait_method_signature_snapshot(method);
	}

	for (GDScriptParser::FunctionNode* method : p_trait->default_methods) {
		if (method == nullptr || method->identifier == nullptr) {
			continue;
		}
		analyzer->resolve_function_signature(method, p_trait);
		gd_trait->default_methods[method->identifier->name] = method;

		gd_trait->required_signatures[method->identifier->name] = _make_trait_method_signature_snapshot(method);
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

	Ref<GDScriptTrait> trait = get_local_trait(trait_name);

	if (trait.is_null()) {
		///not found locally, fall back to global trait registry
		String trait_path = GDScriptCache::get_global_trait_path(trait_name);

		///reminder of a hard fought battle. this guard exists so that if get_local_trait
		///some-fucking-how missed a trait actually declared in this same file, we don't accidentally
		///call into ourselves via get_cached_trait and somehow potentially deadlock ourselves
		///against the fucking cache mutex while this file's own `resolve_trait()` is still running
		///i hate my life
		if (!trait_path.is_empty() && trait_path != parser->get_script_path()) {
			Error trait_err = OK;
			trait = GDScriptCache::get_cached_trait(trait_path, trait_name, trait_err, parser->get_script_path());
			if (trait_err != OK) {
				trait = Ref<GDScriptTrait>();
			}
		}
	}

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
		target_type = parser->current_class->self_type;
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

	///make this impl visible while analyzing its own method bodies. this hack was done because
	///calls that pass `self` to an `impl Trait` parameter inside the impl body can
	///emit a transient invalid-argument error before this impl is registered below
	///basically, passing `self` into a param that wants an `impl Trait` INSIDE the 
	///impl block itself, leading to a nasty cyclic dependency chain
	resolved_impls.push_back(gd_impl);
	p_impl->resolved_gd_impl = gd_impl;

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

		bool is_required = trait->required_methods.has(method_name);
		Ref<GDScriptTraitSignatureSnapshot> required_snapshot;
		if (is_required && trait->required_signatures.has(method_name)) {
			required_snapshot = trait->required_signatures[method_name];
		} else {
			if (trait->required_signatures.has(method_name)) {
				required_snapshot = trait->required_signatures[method_name];
			}
		}

		bool sig_match_result = _signatures_match(required_snapshot, provided);

		if (!sig_match_result) {
			push_error(vformat(R"([Reginleif] Method "%s" does not match the signature required by trait "%s".)", method_name, trait_name), provided);
			ok = false;
			continue;
		}

		gd_impl->provided_methods[method_name] = provided;
		gd_impl->provided_signatures[method_name] = _make_trait_method_signature_snapshot(provided);
	}

	for (GDScriptParser::FunctionNode* provided : p_impl->methods) {
		if (provided == nullptr || provided->identifier == nullptr || !gd_impl->provided_methods.has(provided->identifier->name)) {
			continue;
		}

		GDScriptParser::DataType previous_impl_self_type = analyzer->current_impl_self_type;
		analyzer->current_impl_self_type = target_type;
		analyzer->current_impl_self_type.is_meta_type = false;
		analyzer->current_impl_self_type.is_constant = false;
		analyzer->resolve_function_body(provided);
		analyzer->current_impl_self_type = previous_impl_self_type;
	}

	///Anything not explicitly provided falls back to the trait's own default body(if it has one)
	for (const KeyValue<StringName, GDScriptParser::FunctionNode*>& E : trait->default_methods) {
		if (!gd_impl->provided_methods.has(E.key)) {
			gd_impl->provided_methods[E.key] = E.value;
			if (trait->required_signatures.has(E.key)) {
				gd_impl->provided_signatures[E.key] = trait->required_signatures[E.key];
			}
		}
	}

	resolved_impls.erase(gd_impl);
	p_impl->resolved_gd_impl = Ref<GDScriptImpl>();

	//already got an impl for this trait+type combo? gtfo
	for (const Ref<GDScriptImpl>& existing : resolved_impls) {
		if (existing.is_valid() && existing->trait == trait && existing->impl_target_type == target_type) {
			push_error(vformat(
				R"([Reginleif] Duplicate "impl" of trait "%s" for the same type "%s".)", 
				trait_name, target_type.to_string()),
			p_impl);
			return ERR_PARSE_ERROR;
		}
	}

	///two different traits fighting over the same method name on the same type means that one silently
	///clobbers the other in member_functions at runtime. FUCK. nope, catching it here instead
	for (const Ref<GDScriptImpl>& existing : resolved_impls) {
		if (existing.is_null() || existing->trait == trait) {
			continue; ///same-trait re-impl already handled by the exact-duplicate check above^^^^
		}

		bool same_type = existing->impl_target_type == target_type;
		bool existing_is_more_derived = !same_type && _type_is_or_inherits(existing->impl_target_type, target_type);

		if (existing_is_more_derived) {
			continue; ///case 1, existing impl targets a more specific type, it wins, no collision
		}

		bool target_is_more_derived = !same_type && _type_is_or_inherits(target_type, existing->impl_target_type);

		if (!same_type && !target_is_more_derived) {
			continue; ///case 2, unrelated types entirely, no collision possible
		}

		for (const KeyValue<StringName, GDScriptParser::FunctionNode*>& E : gd_impl->provided_methods) {
			if (!existing->provided_methods.has(E.key)) {
				continue;
			}
			if (target_is_more_derived) {
				continue; ///case 3, we're the more specific type here, we override cleanly
			}

			///case 4, our actual collision
			push_error(vformat(R"([Reginleif] Method "%s" is provided by both trait "%s" and trait "%s" for the same type, so trait dispatch is ambiguous. (Disambiguation is planned in the future, sorry^^'))",
					E.key, trait_name, existing->trait->name), p_impl);
			ok = false;
		}
	}

	///same deal, but in this case we're crossing file boundaries. walk the target's inheritance chain
	///force-resolve any file, that claims something on that chain, 
	///then check for name collisions against our own. simple enough? simple enough. probably. 
	///if you don't get it, this just mirrors the pattern that already exists in the damned analyser.
	GDScriptParser::DataType walk_type = target_type;
	while (ok) {
		StringName walk_key = _impl_target_key(walk_type);

		///if this walk step IS a class declared in the file we're currently analyzing, skip the
		///path-based force-resolve entirely, because comparing derived paths as strings here risks a
		///normalisation mismatch that force-resolves our own file MID FUCKING ANALYSIS, 
		///which is exactly the reentrancy footgun the trait lookup code above already 
		///learnt the hard fucking way
		bool is_current_file_class = walk_type.kind == GDScriptParser::DataType::CLASS &&
				walk_type.class_type != nullptr && parser->has_class(walk_type.class_type);

		if (walk_key != StringName()) {
			if (!is_current_file_class) {
				String owning_path = _owning_path_for_type(walk_type);
				if (!owning_path.is_empty() && !GDScript::is_canonically_equal_paths(owning_path, parser->get_script_path())) {
					///force that file fully resolved so its own impls (and thus its own claims)
					///actually exist before we go trusting the registry for its type key
					Error dep_err = OK;
					GDScriptCache::get_parser(owning_path, GDScriptParserRef::FULLY_SOLVED, dep_err, parser->get_script_path());
				}
			}

			for (const GDScriptCache::GlobalImplClaim& claim : GDScriptCache::get_global_impl_claims(walk_key)) {
				if (claim.trait_name == trait_name && GDScript::is_canonically_equal_paths(claim.owning_path, parser->get_script_path())) {
					continue; /// case 1, that's just us, from a previous pass, not a real collision
				}
				if (claim.trait_name == trait_name) {
					continue; /// case 2, same trait implemented for some random bumfuck ancestor elsewhere, not my problem
				}
				if (!gd_impl->provided_methods.has(claim.method_name)) {
					continue;
				}
				///only the exact type we're impling on cares about the 'derivedness'(?) property , so if we're
				///walking an ancestor, WE are always the more derived side by construction, so any
				///name match here is us legitimately overriding, not colliding. side note i really need a better name than
				///uhh. 'derivedness'.
				if (walk_type != target_type) {
					continue;
				}
				push_error(vformat(R"([Reginleif] Method "%s" is provided by both trait "%s" (in this file) and trait "%s" (in "%s") for the same type. Ambiguous dispatch.)",
						claim.method_name, trait_name, claim.trait_name, claim.owning_path), p_impl);
				ok = false;
			}
		}

		if (walk_type.kind != GDScriptParser::DataType::CLASS || walk_type.class_type == nullptr) {
			break; ///natives don't have a type chain lol
		}
		GDScriptParser::DataType next_walk = walk_type.class_type->base_type;
		if (next_walk.kind == GDScriptParser::DataType::UNRESOLVED) {
			break;
		}
		walk_type = next_walk;
	}

	resolved_impls.push_back(gd_impl);
	p_impl->resolved_gd_impl = gd_impl; ///stash for the compiler, it has no analyzer access of its own

	///register our own claims globally so other files checking THEIR impls against us
	///actually see what WE provided, not just what's local to their own file.
	///makes sure the analyser doesn't fucking shit its pants when wanting to check a claim
	///that doesnt belong to its own file
	StringName our_key = _impl_target_key(target_type);
	if (our_key != StringName()) {
		GDScriptCache::add_global_impl_claims(our_key, trait_name, gd_impl->provided_signatures, parser->get_script_path());
	}

	///how much longer must a man keep fighting for? :sob:

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
	///CLASS declared in this file, or an ENUM declared inside a class we own, counts
	if (p_target_type.kind == GDScriptParser::DataType::CLASS) {
		return p_target_type.class_type != nullptr && parser->has_class(p_target_type.class_type);
	}

	if (p_target_type.kind == GDScriptParser::DataType::ENUM) {
		return p_target_type.class_type != nullptr && parser->has_class(p_target_type.class_type);
	}

	return false;
}

Ref<GDScriptTrait> GDScriptTraitAnalyzer::get_local_trait(const StringName& p_name) const {
	if (resolved_traits.has(p_name)) {
		return resolved_traits[p_name];
	}
	return Ref<GDScriptTrait>();
}

bool GDScriptTraitAnalyzer::_signatures_match(const Ref<GDScriptTraitSignatureSnapshot>& p_required, GDScriptParser::FunctionNode* p_provided) {
	if (p_required.is_null() || p_provided == nullptr) {
		return false;
	}

	if (p_required->param_types.size() != p_provided->parameters.size()) {
		return false;
	}
	if (p_required->is_vararg != p_provided->is_vararg()) {
		return false;
	}

	for (int i = 0; i < p_required->param_types.size(); i++) {
		GDScriptParser::DataType required_param_type = p_required->param_types[i];
		GDScriptParser::DataType provided_param_type = p_provided->parameters[i]->type_constraint;

		///parameters are contravariant in general, but gdscript doesn't do variance gymnastics
		///anywhere else either, so we just require them to line up both ways (effectively equal)
		///if you find this fucky, so do i, congrats!
		if (!analyzer->is_type_compatible(required_param_type, provided_param_type, false) ||
				!analyzer->is_type_compatible(provided_param_type, required_param_type, false)) {
			return false;
		}
	}

	GDScriptParser::DataType required_return = p_required->return_type;
	GDScriptParser::DataType provided_return = p_provided->return_type_constraint;

	const bool required_return_is_void = required_return.kind == GDScriptParser::DataType::BUILTIN && required_return.builtin_type == Variant::NIL && required_return.is_hard_type();
	const bool provided_return_is_void = provided_return.kind == GDScriptParser::DataType::BUILTIN && provided_return.builtin_type == Variant::NIL && provided_return.is_hard_type();
	if (required_return_is_void || provided_return_is_void) {
		return required_return_is_void && provided_return_is_void;
	}

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

	if (p_target_type.kind == GDScriptParser::DataType::BUILTIN) {
		///value types like int/float/String have no inheritance chain, so this is just
		///a straight builtin_type match, ignoring the bullshit that comes with the 
		///DataType flags (hard/meta/etc/whatever the fuck) that would otherwise make
		///the p_type == p_target_type check above too strict.
		return p_type.kind == GDScriptParser::DataType::BUILTIN && p_type.builtin_type == p_target_type.builtin_type;
	}

	if (p_target_type.kind == GDScriptParser::DataType::ENUM) {
		return p_type.kind == GDScriptParser::DataType::ENUM && p_type.native_type == p_target_type.native_type;
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

	if (p_target_type.kind == GDScriptParser::DataType::NATIVE) {
		StringName native_ancestor = p_type.native_type;

		if (native_ancestor == StringName() && p_type.kind == GDScriptParser::DataType::CLASS) {
			///a script class doesn't carry its own, native_type directly, 
			///so walk up its CLASS chain until we bottom out at the first NATIVE ancestor, 
			///then compare from there. this is what lets `impl for <builtin>` 
			///be reachable via implicit self on a Node-or-whatever-derived script
			GDScriptParser::ClassNode* current = p_type.class_type;
			while (current != nullptr && current->base_type.kind == GDScriptParser::DataType::CLASS) {
				current = current->base_type.class_type;
			}
			if (current != nullptr && current->base_type.kind == GDScriptParser::DataType::NATIVE) {
				native_ancestor = current->base_type.native_type;
			}
		}

		if (native_ancestor == StringName()) {
			return false;
		}

		return ClassDB::is_parent_class(native_ancestor, p_target_type.native_type);
	}

	return false;
}

StringName GDScriptTraitAnalyzer::_impl_target_key(const GDScriptParser::DataType& p_type) const {
	if (p_type.kind == GDScriptParser::DataType::BUILTIN) {
		///value types like int/String/Vector2. identified by the Variant::Type enum itself
		return StringName("builtin::" + itos((int)p_type.builtin_type));
	}
	if (p_type.kind == GDScriptParser::DataType::NATIVE) {
		//native Object-derived classes like Node, RefCounted. identified by name
		return StringName("native::" + String(p_type.native_type));
	}
	if (p_type.kind == GDScriptParser::DataType::CLASS && p_type.class_type != nullptr) {
		return StringName("class::" + p_type.class_type->fqcn);
	}
	if (p_type.kind == GDScriptParser::DataType::ENUM) {
		return StringName("enum::" + String(p_type.native_type));
	}
	return StringName(); ///not a type we do cross-file impl tracking for
}

String GDScriptTraitAnalyzer::_owning_path_for_type(const GDScriptParser::DataType& p_type) const {
	if (p_type.kind == GDScriptParser::DataType::CLASS && p_type.class_type != nullptr) {
		return p_type.class_type->fqcn.get_slice("::", 0);
	}
	return String(); ///natives don't live in any file, nothing to force-resolve
}

bool GDScriptTraitAnalyzer::type_satisfies_trait(const GDScriptParser::DataType& p_concrete_type, const Ref<GDScriptTrait>& p_trait) const {
	if (p_trait.is_null()) {
		return false;
	}

	for (const Ref<GDScriptImpl>& impl : resolved_impls) {
		if (impl.is_null() || impl->trait != p_trait) {
			continue;
		}

		if (_type_is_or_inherits(p_concrete_type, impl->impl_target_type)) {
			return true;
		}
	}

	///not satisfied locally, check the global impl-claim registry for cross-file impls!
	return global_claims_satisfy_trait(p_concrete_type, p_trait->name);
}

bool GDScriptTraitAnalyzer::global_claims_satisfy_trait(const GDScriptParser::DataType& p_concrete_type, const StringName& p_trait_name) const {
	GDScriptParser::DataType walk_type = p_concrete_type;

	while (true) {
		StringName walk_key = _impl_target_key(walk_type);

		if (walk_key != StringName()) {
			for (const GDScriptCache::GlobalImplClaim& claim : GDScriptCache::get_global_impl_claims(walk_key)) {
				if (claim.trait_name == p_trait_name) {
					return true;
				}
			}
		}

		if (walk_type.kind == GDScriptParser::DataType::CLASS && walk_type.class_type != nullptr) {
			GDScriptParser::DataType next_walk = walk_type.class_type->base_type;
			if (next_walk.kind == GDScriptParser::DataType::UNRESOLVED) {
				break;
			}
			walk_type = next_walk;
			continue;
		}

		if (walk_type.kind == GDScriptParser::DataType::NATIVE && walk_type.native_type != StringName()) {
			StringName parent_native = ClassDB::get_parent_class(walk_type.native_type);
			if (parent_native == StringName()) {
				break;
			}
			GDScriptParser::DataType native_walk;
			native_walk.kind = GDScriptParser::DataType::NATIVE;
			native_walk.builtin_type = Variant::OBJECT;
			native_walk.native_type = parent_native;
			walk_type = native_walk;
			continue;
		}

		break;
	}

	return false;
}

GDScriptParser::FunctionNode* GDScriptTraitAnalyzer::find_impl_method(const GDScriptParser::DataType& p_base_type, const StringName& p_method_name) const {
	Ref<GDScriptImpl> best_match;

	for (const Ref<GDScriptImpl>& impl : resolved_impls) {
		if (impl.is_null() || !impl->provided_methods.has(p_method_name)) {
			continue;
		}
		if (!_type_is_or_inherits(p_base_type, impl->impl_target_type)) {
			continue;
		}
		if (best_match.is_null() || _type_is_or_inherits(impl->impl_target_type, best_match->impl_target_type)) {
			///impl's target is same-or-more-derived than what we've matched so far
			best_match = impl;
		}
	}

	return best_match.is_valid() ? best_match->provided_methods[p_method_name] : nullptr;
}

Ref<GDScriptTraitSignatureSnapshot> GDScriptTraitAnalyzer::find_impl_method_signature(const GDScriptParser::DataType& p_base_type, const StringName& p_method_name) const {
	Ref<GDScriptImpl> best_match;

	for (const Ref<GDScriptImpl>& impl : resolved_impls) {
		if (impl.is_null() || !impl->provided_signatures.has(p_method_name)) {
			continue;
		}
		if (!_type_is_or_inherits(p_base_type, impl->impl_target_type)) {
			continue;
		}
		if (best_match.is_null() || _type_is_or_inherits(impl->impl_target_type, best_match->impl_target_type)) {
			///impl's target is same-or-more-derived than what we've matched so far
			best_match = impl;
		}
	}

	return best_match.is_valid() ? best_match->provided_signatures[p_method_name] : Ref<GDScriptTraitSignatureSnapshot>();
}
