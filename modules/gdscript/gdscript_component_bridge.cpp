/**************************************************************************/
/*  gdscript_component_bridge                                             */
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

#include "gdscript.h"
#include "program_index.h"

#include "core/object/script_language.h"
#include "scene/main/component.h"
#include "scene/main/composer.h"
#include "scene/main/node.h"

void GDScript::get_attach_constraints(StringName& r_base, Vector<StringName>& r_traits) const {
	Ref<GDScript> parent = base;
	if (parent.is_valid()) {
		parent->get_attach_constraints(r_base, r_traits);
	}
	if (attach_to_base != StringName() && (r_base == StringName() || Composer::class_is_a(attach_to_base, r_base))) {
		r_base = attach_to_base;
	}
	for (const StringName& t : attach_to_traits) {
		if (!r_traits.has(t)) {
			r_traits.push_back(t);
		}
	}
}

void GDScript::get_sibling_deps(Vector<SiblingDep>& r_deps) const {
	Ref<GDScript> parent = base;
	if (parent.is_valid()) {
		parent->get_sibling_deps(r_deps);
	}
	for (const SiblingDep& d : sibling_deps) {
		r_deps.push_back(d);
	}
}

static bool _component_host_implements(const Node* p_host, const StringName& p_trait) {
	RGIndex::ProgramIndexService* svc = RGIndex::ProgramIndexService::get_singleton();
	if (svc == nullptr || !svc->is_populated()) {
		return false;
	}
	RGIndex::IndexView view(svc->get_snapshot(), nullptr);

	Ref<GDScript> script = p_host->get_script();
	if (script.is_valid() && !script->get_path().is_empty()) {
		const RGIndex::TypeKey key = RGIndex::TypeKey::script_class(StringName(RGIndex::canonical_path(script->get_path())));
		if (view.implements(key, p_trait)) {
			return true;
		}
	}
	return view.implements(RGIndex::TypeKey::native(p_host->get_class_name()), p_trait);
}

static void _component_attach_constraints(const Ref<Script>& p_script, StringName& r_base, Vector<StringName>& r_traits) {
	const GDScript* gd = Object::cast_to<GDScript>(p_script.ptr());
	if (gd != nullptr) {
		gd->get_attach_constraints(r_base, r_traits);
	}
}

static void _component_sibling_deps(const Ref<Script>& p_script, Vector<Component::SiblingDep>& r_deps) {
	const GDScript* gd = Object::cast_to<GDScript>(p_script.ptr());
	if (gd == nullptr) {
		return;
	}

	Vector<GDScript::SiblingDep> deps;
	gd->get_sibling_deps(deps);
	for (const GDScript::SiblingDep& d : deps) {
		Component::SiblingDep out;
		out.member = d.member;
		out.type = d.type;
		out.required = d.required;
		r_deps.push_back(out);
	}
}

static bool _component_debug_break(const String& p_message, bool p_allow_continue) {
	GDScriptLanguage* lang = GDScriptLanguage::get_singleton();
	return lang != nullptr && lang->debug_break(p_message, p_allow_continue);
}

void register_component_support() {
	Composer::trait_check = &_component_host_implements;
	Composer::debug_break_hook = &_component_debug_break;
	Component::sibling_deps_provider = &_component_sibling_deps;
	Component::attach_constraints_provider = &_component_attach_constraints;
}
