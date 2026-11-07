/**************************************************************************/
/*  composer.cpp                                                          */
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

#include "composer.h"

#include "core/config/engine.h"
#include "core/debugger/engine_debugger.h"
#include "core/error/error_macros.h"
#include "core/templates/hash_set.h"
#include "core/object/class_db.h"
#include "core/object/callable_mp.h"
#include "core/object/script_language.h"
#include "scene/main/component.h"

Composer::TraitCheckFn Composer::trait_check = nullptr;
Composer::DebugBreakFn Composer::debug_break_hook = nullptr;

static HashSet<Composer*> live_composers;

static bool _node_is_class(const Node* p_node, const StringName& p_class) {
	if (p_node->is_class(p_class)) {
		return true;
	}
	Ref<Script> s = p_node->get_script();
	while (s.is_valid()) {
		if (s->get_global_name() == p_class) {
			return true;
		}
		s = s->get_base_script();
	}
	return false;
}

void Composer::fail_contract(const String& p_msg) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	ERR_PRINT(p_msg);
#ifdef DEBUG_ENABLED
	if (EngineDebugger::is_active() && debug_break_hook != nullptr) {
		debug_break_hook(p_msg, false);
	}
#endif
}

bool Composer::_host_is_class(const StringName& p_class) const {
	if (host == nullptr) {
		return false;
	}

	if (p_class == StringName() || host->is_class(p_class)) {
		return true;
	}

	Ref<Script> s = host->get_script();
	while (s.is_valid()) {
		if (s->get_global_name() == p_class) {
			return true;
		}
		s = s->get_base_script();
	}
	return false;
}

bool Composer::_host_satisfies(const StringName& p_base, const Vector<StringName>& p_traits, const String& p_who, String& r_error) const {
	
	if (host == nullptr) {
		r_error = vformat(R"(%s has no host. The Composer is not parented.)", p_who);
		return false;
	}

	if (!_host_is_class(p_base)) {
		r_error = vformat(R"(%s can only attach to %s, but the host "%s" is a %s.)", p_who, p_base, host->get_name(), host->get_class());
		return false;
	}
	for (const StringName& t : p_traits) {
		if (trait_check == nullptr || !trait_check(host, t)) {
			r_error = vformat(R"(%s needs a host that implements %s, but "%s" does not.)", p_who, t, host->get_name());
			return false;
		}
	}
	return true;
}

bool Composer::_validate(Component* p_component, String& r_error) const {
	StringName base;
	Vector<StringName> traits;
	p_component->get_attach_constraints(base, traits);
	return _host_satisfies(base, traits, vformat(R"(Component "%s")", p_component->get_name()), r_error);
}

void Composer::aggregate_constraints(Component* p_extra, StringName& r_base, Vector<StringName>& r_traits, String& r_conflict) const {
	r_conflict = String();
	r_base = extra_base;
	r_traits = extra_traits;
	const int n = get_child_count(false) + (p_extra != nullptr ? 1 : 0);
	for (int i = 0; i < n; i++) {
		Component* c = i < get_child_count(false) ? Object::cast_to<Component>(get_child(i, false)) : p_extra;
		if (c == nullptr) {
			continue;
		}

		StringName b;
		Vector<StringName> traits;
		c->get_attach_constraints(b, traits);

		for (const StringName& t : traits) {
			if (!r_traits.has(t)) {
				r_traits.push_back(t);
			}
		}

		if (b == StringName() || b == r_base) {
			continue;
		}

		if (r_base == StringName() || class_is_a(b, r_base)) {
			r_base = b;
		} else if (!class_is_a(r_base, b) && r_conflict.is_empty()) {
			r_conflict = vformat(R"(Component "%s" needs a "%s" host, but another constraint needs "%s". No host can be both.)", c->get_name(), b, r_base);
		}

	}
}

bool Composer::accepts_host(const Node* p_host, String& r_error) const {
	StringName base;
	Vector<StringName> traits;
	String conflict;
	aggregate_constraints(nullptr, base, traits, conflict);

	if (!conflict.is_empty()) {
		r_error = conflict;
		return false;
	}
	return host_satisfies(p_host, base, traits, vformat(R"(Composer "%s")", get_name()), r_error);
}

int Composer::_find_entry(ObjectID p_id) const {
	for (int i = 0; i < entries.size(); i++) {
		if (entries[i].id == p_id) {
			return i;
		}
	}
	return -1;
}

void Composer::_reorder() {
	Vector<Entry> ordered;
	for (int i = 0; i < get_child_count(false); i++) {
		Component* c = Object::cast_to<Component>(get_child(i, false));
		if (c == nullptr) {
			continue;
		}

		const int old = _find_entry(c->get_instance_id());
		if (old >= 0) {
			ordered.push_back(entries[old]);
		}
	}

	entries = ordered;
	entries_dirty = false;
}

void Composer::_compact() {
	for (int i = entries.size() - 1; i >= 0; i--) {
		if (ObjectDB::get_instance(entries[i].id) == nullptr) {
			entries.remove_at(i);
		}
	}
}

void Composer::_refresh_processing() {
	bool phys = false, proc = false;
	for (const Entry& e : entries) {
		phys = phys || (e.valid && e.has_physics);
		proc = proc || (e.valid && e.has_process);
	}

	set_physics_process_internal(attached && phys);
	set_process_internal(attached && proc);
}

void Composer::_attach_entry(Entry& r_entry, Component* p_component) {
	String err;
	r_entry.valid = _validate(p_component, err);

	if (!r_entry.valid) {
		fail_contract(err);
		return;
	}

	r_entry.has_physics = p_component->has_physics_tick();
	r_entry.has_process = p_component->has_tick();
	p_component->_call_on_attach();
}

void Composer::_on_child_entered(Node* p_child) {
	Component* c = Object::cast_to<Component>(p_child);
	if (c == nullptr) {
		return;
	}
	c->_set_host(host);
	_resolve_siblings();

	Entry e;
	e.id = c->get_instance_id();

	if (attached) {
		_attach_entry(e, c);
	}

	entries.push_back(e);
	entries_dirty = true;
	_refresh_processing();
	update_configuration_warnings();
	if (is_ready()) {
		_queue_dependency_check();
	}

}

void Composer::_on_child_exiting(Node* p_child) {
	Component* c = Object::cast_to<Component>(p_child);
	if (c == nullptr) {
		return;
	}
	const int idx = _find_entry(c->get_instance_id());

	if (idx >= 0) {
		if (attached && entries[idx].valid) {
			c->_call_on_detach();
		}
		if (ticking) {
			entries.write[idx].valid = false;
		} else {
			entries.remove_at(idx);
		}
	}

	c->_set_host(nullptr);
	_resolve_siblings(c);
	_refresh_processing();
	update_configuration_warnings();

	if (is_ready()) {
		_queue_dependency_check();
	}
}

void Composer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PARENTED: {
			host = get_parent();
			for (const Entry& e : entries) {
				Component* c = Object::cast_to<Component>(ObjectDB::get_instance(e.id));
				if (c != nullptr) {
					c->_set_host(host);
				}
			}
		} break;

		case NOTIFICATION_UNPARENTED: {
			for (const Entry& e : entries) {
				Component* c = Object::cast_to<Component>(ObjectDB::get_instance(e.id));
				if (c != nullptr) {
					c->_set_host(nullptr);
				}
			}
			host = nullptr;
		} break;

		case NOTIFICATION_ENTER_TREE: {
			attached = true;
			String err;
			if (!_host_satisfies(extra_base, extra_traits, "Composer", err)) {
				fail_contract(err);
			}

			StringName agg_base;
			Vector<StringName> agg_traits;
			String conflict;
			aggregate_constraints(nullptr, agg_base, agg_traits, conflict);
			if (!conflict.is_empty()) {
				fail_contract(conflict);
			}
		} break;

		case NOTIFICATION_READY: {
			_check_dependencies();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			attached = false;
			_refresh_processing();
		} break;

		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}
			if (entries_dirty) {
				_reorder();
			}
			const double delta = get_physics_process_delta_time();
			ticking = true;
			const int n = entries.size();
			for (int i = 0; i < n; i++) {
				const Entry& e = entries[i];
				if (!e.valid || !e.has_physics) {
					continue;
				}
				Component* c = Object::cast_to<Component>(ObjectDB::get_instance(e.id));
				if (c != nullptr) {
					c->_call_physics_tick(delta);
				}
			}
			ticking = false;
			_compact();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			if (Engine::get_singleton()->is_editor_hint()) {
				break;
			}
			if (entries_dirty) {
				_reorder();
			}
			const double delta = get_process_delta_time();
			ticking = true;
			const int n = entries.size();
			for (int i = 0; i < n; i++) {
				const Entry& e = entries[i];
				if (!e.valid || !e.has_process) {
					continue;
				}
				Component* c = Object::cast_to<Component>(ObjectDB::get_instance(e.id));
				if (c != nullptr) {
					c->_call_tick(delta);
				}
			}

			ticking = false;
			_compact();

		} break;

		case NOTIFICATION_CHILD_ORDER_CHANGED: {
			entries_dirty = true;
		} break;
	}
}

Component* Composer::get_component(const StringName& p_class) const {
	for (const Entry& e : entries) {
		Component* c = Object::cast_to<Component>(ObjectDB::get_instance(e.id));
		if (c == nullptr || !e.valid) {
			continue;
		}
		if (c->is_class(p_class)) {
			return c;
		}
		Ref<Script> s = c->get_script();
		while (s.is_valid()) {
			if (s->get_global_name() == p_class) {
				return c;
			}
			s = s->get_base_script();
		}
	}
	return nullptr;
}

void Composer::set_extra_base(const StringName& p_base) {
	extra_base = p_base;
	update_configuration_warnings();
}

void Composer::set_extra_traits(const Vector<StringName>& p_traits) {
	extra_traits = p_traits;
	update_configuration_warnings();
}

PackedStringArray Composer::get_configuration_warnings() const {
	PackedStringArray warnings = Node::get_configuration_warnings();
	if (host == nullptr) {
		warnings.push_back(RTR("A Composer needs a parent to be the host."));
	}
	for (int i = 0; i < get_child_count(false); i++) {
		Node* child = get_child(i, false);
		if (Object::cast_to<Component>(child) == nullptr) {
			warnings.push_back(vformat(RTR(R"(Child "%s" is not a Component, so this Composer ignores it.)"), child->get_name()));
		}
	}

	return warnings;
}

void Composer::get_contract_errors(Vector<String>& r_errors) const {
	if (host == nullptr) {
		return; 
	}
	String err;

	if (!_host_satisfies(extra_base, extra_traits, "This Composer", err)) {
		r_errors.push_back(err);
	}

	for (int i = 0; i < get_child_count(false); i++) {
		Component* c = Object::cast_to<Component>(get_child(i, false));
		if (c == nullptr) {
			continue;
		}
		if (!_validate(c, err)) {
			r_errors.push_back(err);
		}
		get_dependency_errors(c, r_errors);
	}

	StringName agg_base;
	Vector<StringName> agg_traits;
	String conflict;
	aggregate_constraints(nullptr, agg_base, agg_traits, conflict);
	if (!conflict.is_empty()) {
		r_errors.push_back(conflict);
	}
}

int Composer::_find_siblings(const StringName& p_class, const Node* p_self, const Node* p_exclude, Node*& r_first) const {
	r_first = nullptr;
	int found = 0;

	for (int i = 0; i < get_child_count(false); i++) {
		Node* child = get_child(i, false);
		if (child == p_self || child == p_exclude || Object::cast_to<Component>(child) == nullptr) {
			continue;
		}
		if (_node_is_class(child, p_class)) {
			if (found == 0) {
				r_first = child;
			}
			found++;
		}
	}

	return found;
}

///throw in @requires and @if_present deps
void Composer::_resolve_siblings(const Node* p_exclude) {
	for (int i = 0; i < get_child_count(false); i++) {
		Component* c = Object::cast_to<Component>(get_child(i, false));
		if (c == nullptr || c == p_exclude) {
			continue;
		}
		Vector<Component::SiblingDep> deps;
		c->get_sibling_deps(deps);
		for (const Component::SiblingDep& d : deps) {
			Node* first = nullptr;
			const int found = _find_siblings(d.type, c, p_exclude, first);
			bool valid = false;
			c->set(d.member, found == 1 ? Variant(first) : Variant(), &valid);
		}
	}
}

void Composer::get_dependency_errors(const Component* p_component, Vector<String>& r_errors) const {
	ERR_FAIL_NULL(p_component);
	Vector<Component::SiblingDep> deps;
	p_component->get_sibling_deps(deps);

	for (const Component::SiblingDep& d : deps) {
		Node* first = nullptr;
		const int found = _find_siblings(d.type, p_component, nullptr, first);
		if (found == 0 && d.required) {
			r_errors.push_back(vformat(R"(Component "%s" requires a "%s" sibling (for "%s"), but this Composer has none.)", p_component->get_name(), d.type, d.member));
		} else if (found > 1) {
			r_errors.push_back(vformat(R"(Component "%s" looks up its "%s" sibling (for "%s"), but this Composer has %d of them. Only one is allowed.)", p_component->get_name(), d.type, d.member, found));
		}
	}
}

void Composer::_check_dependencies() {
	deps_check_queued = false;
	if (!attached) {
		return;
	}
	Vector<String> errors;
	for (int i = 0; i < get_child_count(false); i++) {
		const Component* c = Object::cast_to<Component>(get_child(i, false));
		if (c != nullptr) {
			get_dependency_errors(c, errors);
		}
	}
	for (const String& e : errors) {
		fail_contract(e);
	}
}

void Composer::_queue_dependency_check() {
	if (deps_check_queued || !attached) {
		return;
	}
	deps_check_queued = true;
	callable_mp(this, &Composer::_check_dependencies).call_deferred();
}

void Composer::_revalidate() {
	for (Entry& e : entries) {
		Component* comp = Object::cast_to<Component>(ObjectDB::get_instance(e.id));
		if (comp == nullptr) {
			continue;
		}

		String err;
		const bool ok = _validate(comp, err);
		if (e.valid && !ok) {
			if (attached) {
				comp->_call_on_detach();
			}
			e.valid = false;
			fail_contract(err);
		} else if (!e.valid && ok && attached) {
			_attach_entry(e, comp);
		}
	}
	_resolve_siblings();
	_check_dependencies();
	_refresh_processing();
	update_configuration_warnings(); ///the editor refreshes its dock off this
}

void Composer::revalidate_all() {
	for (Composer* c : live_composers) {
		c->_revalidate();
	}
}

Composer::~Composer() {
	live_composers.erase(this);
}

void Composer::_bind_methods() {
	///i'm tired... yet again...
	ClassDB::bind_method(D_METHOD("get_host"), &Composer::get_host);
	ClassDB::bind_method(D_METHOD("get_component", "class_name"), &Composer::get_component);

	ClassDB::bind_method(D_METHOD("set_extra_base", "base"), &Composer::set_extra_base);
	ClassDB::bind_method(D_METHOD("get_extra_base"), &Composer::get_extra_base);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "extra_base"), "set_extra_base", "get_extra_base");

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "host", PROPERTY_HINT_NODE_TYPE, "Node", PROPERTY_USAGE_NONE), "", "get_host");
}

bool Composer::can_accept(Component* p_component, String& r_error) const {
	ERR_FAIL_NULL_V(p_component, false);

	if (host != nullptr && !_validate(p_component, r_error)) {
		return false;
	}

	StringName agg_base;
	Vector<StringName> agg_traits;
	String conflict;
	aggregate_constraints(p_component, agg_base, agg_traits, conflict);
	if (!conflict.is_empty()) {
		r_error = conflict;
		return false;
	}
	return true;
}

bool Composer::class_is_a(const StringName& p_derived, const StringName& p_base) {
	StringName c = p_derived;
	for (int guard = 0; c != StringName() && guard < 64; guard++) {
		if (c == p_base) {
			return true;
		}

		if (ScriptServer::is_global_class(c)) {
			c = ScriptServer::get_global_class_base(c);
		} else {
			return ClassDB::class_exists(c) && ClassDB::is_parent_class(c, p_base);
		}
	}
	return false;
}

Composer* Composer::find_composer(const Node* p_host) {
	ERR_FAIL_NULL_V(p_host, nullptr);
	for (int i = 0; i < p_host->get_child_count(false); i++) {
		Composer* h = Object::cast_to<Composer>(p_host->get_child(i, false));
		if (h != nullptr) {
			return h;
		}
	}
	return nullptr;
}

Composer::Composer() {
	live_composers.insert(this);
	connect(SNAME("child_entered_tree"), callable_mp(this, &Composer::_on_child_entered));
	connect(SNAME("child_exiting_tree"), callable_mp(this, &Composer::_on_child_exiting));
}

bool Composer::host_accepts(const Node* p_host, Component* p_component, String& r_error) {
	ERR_FAIL_NULL_V(p_component, false);
	StringName base;
	Vector<StringName> traits;
	p_component->get_attach_constraints(base, traits);
	return host_satisfies(p_host, base, traits, vformat(R"(Component "%s")", p_component->get_name()), r_error);
}

bool Composer::host_satisfies(const Node* p_host, const StringName& p_base, const Vector<StringName>& p_traits, const String& p_who, String& r_error) {
	ERR_FAIL_NULL_V(p_host, false);
	bool class_ok = p_base == StringName() || p_host->is_class(p_base);

	if (!class_ok) {
		Ref<Script> s = p_host->get_script();
		while (s.is_valid() && !class_ok) {
			class_ok = s->get_global_name() == p_base;
			s = s->get_base_script();
		}
	}

	if (!class_ok) {
		r_error = vformat(R"(%s can only attach to "%s", but "%s" is a "%s".)", p_who, p_base, p_host->get_name(), p_host->get_class());
		return false;
	}

	for (const StringName& t : p_traits) {
		if (trait_check == nullptr || !trait_check(p_host, t)) {
			r_error = vformat(R"(%s needs a host that implements "%s", but "%s" does not.)", p_who, t, p_host->get_name());
			return false;
		}
	}
	return true;
}
