/**************************************************************************/
/*  composer.h                                                            */
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

#include "scene/main/node.h"

class Component;

class Composer : public Node {
	GDCLASS(Composer, Node);

public:
	typedef bool (*TraitCheckFn)(const Node* p_host, const StringName& p_trait);
	static TraitCheckFn trait_check;

	typedef bool (*DebugBreakFn)(const String& p_message, bool p_allow_continue);
	static DebugBreakFn debug_break_hook;

	StringName extra_base;
	Vector<StringName> extra_traits;

private:
	struct Entry {
		ObjectID id;
		bool valid = false;
		bool has_physics = false;
		bool has_process = false;
	};

	Node* host = nullptr;
	Vector<Entry> entries;
	bool entries_dirty = false;
	bool ticking = false;
	bool attached = false;
	bool deps_check_queued = false;

	int _find_entry(ObjectID p_id) const;
	void _reorder();
	void _compact();
	void _refresh_processing();
	bool _host_is_class(const StringName& p_class) const;
	bool _host_satisfies(const StringName& p_base, const Vector<StringName>& p_traits, const String& p_who, String& r_error) const;
	bool _validate(Component* p_component, String& r_error) const;
	void _revalidate();
	int _find_siblings(const StringName& p_class, const Node* p_self, const Node* p_exclude, Node*& r_first) const;
	void _resolve_siblings(const Node* p_exclude = nullptr);
	void _check_dependencies();
	void _queue_dependency_check();
	void _attach_entry(Entry& r_entry, Component* p_component);
	void _on_child_entered(Node* p_child);
	void _on_child_exiting(Node* p_child);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	Node* get_host() const { return host; }

	void set_extra_base(const StringName& p_base);
	StringName get_extra_base() const { return extra_base; }
	void set_extra_traits(const Vector<StringName>& p_traits);
	Vector<StringName> get_extra_traits() const { return extra_traits; }

	Component* get_component(const StringName& p_class) const;

	bool can_accept(Component* p_component, String& r_error) const;

	void aggregate_constraints(Component* p_extra, StringName& r_base, Vector<StringName>& r_traits, String& r_conflict) const;

	bool accepts_host(const Node* p_host, String& r_error) const;

	static bool class_is_a(const StringName& p_derived, const StringName& p_base);

	static Composer* find_composer(const Node* p_host);

	static bool host_accepts(const Node* p_host, Component* p_component, String& r_error);

	static bool host_satisfies(const Node* p_host, const StringName& p_base, const Vector<StringName>& p_traits, const String& p_who, String& r_error);

	PackedStringArray get_configuration_warnings() const override;

	Composer();
	~Composer();

	static void fail_contract(const String& p_msg);

	void get_contract_errors(Vector<String>& r_errors) const;

	void get_dependency_errors(const Component* p_component, Vector<String>& r_errors) const;

	static void revalidate_all();
};
