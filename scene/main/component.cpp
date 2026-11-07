/**************************************************************************/
/*  component.cpp                                                         */
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

#include "component.h"

#include "core/object/script_language.h"
#include "core/object/class_db.h"
#include "scene/main/composer.h"

bool Component::has_physics_tick() const {
	return GDVIRTUAL_IS_OVERRIDDEN(_physics_tick);
}

bool Component::has_tick() const {
	return GDVIRTUAL_IS_OVERRIDDEN(_tick);
}

void Component::_call_on_attach() {
	GDVIRTUAL_CALL(_on_attach);
}

void Component::_call_on_detach() {
	GDVIRTUAL_CALL(_on_detach);
}

void Component::_call_physics_tick(double p_delta) {
	GDVIRTUAL_CALL(_physics_tick, p_delta);
}

void Component::_call_tick(double p_delta) {
	GDVIRTUAL_CALL(_tick, p_delta);
}

Component::AttachConstraintsFn Component::attach_constraints_provider = nullptr;
Component::SiblingDepsFn Component::sibling_deps_provider = nullptr;

void Component::get_sibling_deps(Vector<SiblingDep>& r_deps) const {
	Ref<Script> script = get_script();
	if (script.is_valid() && sibling_deps_provider != nullptr) {
		sibling_deps_provider(script, r_deps);
	}
}

void Component::get_script_attach_constraints(const Ref<Script>& p_script, StringName& r_base, Vector<StringName>& r_traits) {
	r_base = StringName();
	r_traits.clear();
	if (p_script.is_valid() && attach_constraints_provider != nullptr) {
		attach_constraints_provider(p_script, r_base, r_traits);
	}
}

void Component::get_attach_constraints(StringName& r_base, Vector<StringName>& r_traits) const {
	get_script_attach_constraints(get_script(), r_base, r_traits);
}

void Component::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (Object::cast_to<Composer>(get_parent()) == nullptr && is_inside_tree()) {
				Composer::fail_contract(vformat(R"(Component "%s" must be a direct child of a Composer.)", get_path()));
			}
		} break;
	}
}

void Component::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_host"), &Component::get_host);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "host", PROPERTY_HINT_NODE_TYPE, "Node", PROPERTY_USAGE_NONE), "", "get_host");

	GDVIRTUAL_BIND(_on_attach);
	GDVIRTUAL_BIND(_physics_tick, "delta");
	GDVIRTUAL_BIND(_tick, "delta");
	GDVIRTUAL_BIND(_on_detach);
}
