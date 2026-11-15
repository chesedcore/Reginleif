/**************************************************************************/
/*  gdscript_function.cpp                                                 */
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

#include "gdscript_function.h"

#include "gdscript.h"
#include "gdscript_cache.h"

#include "core/object/class_db.h"
#include "core/variant/container_type_validate.h"

ContainerTypeValidate GDScriptDataType::make_element_validator() const {
	ContainerTypeValidate validator;
	if (generic_param_index >= 0) {
		validator.param_index = (int16_t)generic_param_index;
		validator.has_param = true;
		return validator;
	}

	validator.variant_type = builtin_type;
	validator.class_name = native_type;
	if (enum_native_type != StringName()) {
		validator.enum_id = (int16_t)GDScriptLanguage::get_singleton()->get_or_add_enum_id(enum_native_type);
	}

	if (script_type != nullptr) {
		validator.script = Ref<Script>(script_type);
	}

	int count = 0;
	if (has_container_element_types()) {
		if (kind == BUILTIN) {
			if (builtin_type == Variant::ARRAY) {
				count = 1;
			} else if (builtin_type == Variant::DICTIONARY) {
				count = 2;
			}
		} else if (kind == GDSCRIPT || kind == SCRIPT) {
			count = container_element_types.size();
		}
	}

	for (int i = 0; i < count; i++) {
		validator.nested_types.push_back(get_container_element_type_or_variant(i).make_element_validator());
		validator.has_param = validator.has_param || validator.nested_types[i].has_param;
	}
	return validator;
}

String GDScriptFunction::describe_validator(const ContainerTypeValidate& p_validator) {
	if (p_validator.param_index >= 0) {
		return "<type arg " + itos(p_validator.param_index) + ">";
	}

	String name;
	if (p_validator.script.is_valid()) {
		name = GDScript::debug_get_script_name(p_validator.script);
	} else if (p_validator.class_name != StringName()) {
		name = p_validator.class_name;
	} else if (p_validator.enum_id > 0) {
		name = GDScriptLanguage::get_singleton()->get_enum_name_from_id(p_validator.enum_id);
	} else if (p_validator.variant_type == Variant::NIL) {
		name = "Variant";
	} else {
		name = Variant::get_type_name(p_validator.variant_type);
	}
	if (p_validator.nested_types.size() > 0) {
		name += "[";
		for (int i = 0; i < p_validator.nested_types.size(); i++) {
			if (i > 0) {
				name += ", ";
			}
			name += describe_validator(p_validator.nested_types[i]);
		}
		name += "]";
	}
	return name;
}

bool GDScriptFunction::resolve_instance_type_args(GDScriptInstance* p_instance, const Script* p_target, Vector<ContainerTypeValidate>& r_args) {
	Vector<ContainerTypeValidate> args = p_instance->type_args;
	GDScript* current = p_instance->script.ptr();
	while (current != nullptr) {
		if (current == p_target) {
			r_args = args;
			return true;
		}
		Vector<ContainerTypeValidate> base_args;
		base_args.resize(current->base_type_args.size());
		for (int i = 0; i < base_args.size(); i++) {
			base_args.write[i] = current->base_type_args[i].resolved(args.ptr(), args.size());
		}
		args = base_args;
		current = current->base.ptr();
	}
	return false;
}

bool GDScriptFunction::generic_instance_matches(Object* p_object, const ContainerTypeValidate& p_expected) {
	if (p_object == nullptr) {
		return true;
	}
	ScriptInstance* script_instance = p_object->get_script_instance();
	if (script_instance == nullptr || script_instance->get_language() != GDScriptLanguage::get_singleton()) {
		return false;
	}

	Vector<ContainerTypeValidate> args;
	if (!resolve_instance_type_args(static_cast<GDScriptInstance *>(script_instance), p_expected.script.ptr(), args)) {
		return false;
	}
	if (args.size() != p_expected.nested_types.size()) {
		return false;
	}

	for (int i = 0; i < args.size(); i++) {
		if (args[i] != p_expected.nested_types[i]) {
			return false;
		}
	}
	return true;
}

GDScriptFunction* GDScriptFunction::find_static_impl_function(const ContainerTypeValidate& p_type, const StringName& p_method) {
	GDScriptLanguage* language = GDScriptLanguage::get_singleton();

	for (int attempt = 0; attempt < 2; attempt++) {
		GDScriptFunction* found = nullptr;
		if (p_type.script.is_valid()) {
			const GDScript* current = Object::cast_to<GDScript>(p_type.script.ptr());

			while (current != nullptr && found == nullptr) {

				HashMap<StringName, GDScriptFunction*>::ConstIterator E = current->member_functions.find(p_method);
				if (E && E->value != nullptr && E->value->is_static()) {
					found = E->value;
					break;
				}
				
				GDScriptFunction* provided = language->get_script_class_impl_method(StringName(current->fully_qualified_name), p_method);
				if (provided != nullptr && provided->is_static()) {
					found = provided;
					break;
				}

				current = current->base.ptr();
			}

		} else if (p_type.class_name != StringName()) {
			if (attempt == 1) {
				GDScriptCache::ensure_impl_providers_loaded_for_native_class(p_type.class_name);
			}
			found = language->find_native_class_impl_method_cached(p_type.class_name, p_method);

		} else if (p_type.enum_id > 0) {
			if (attempt == 1) {
				GDScriptCache::ensure_impl_providers_loaded_for_enums();
			}
			found = language->get_enum_impl_method(language->get_enum_name_from_id(p_type.enum_id), p_method);

		} else if (p_type.variant_type != Variant::NIL && p_type.variant_type != Variant::OBJECT) {
			if (attempt == 1) {
				GDScriptCache::ensure_impl_providers_loaded_for_builtin(p_type.variant_type);
			}
			found = language->get_native_impl_method(p_type.variant_type, p_method);
		}
		if (found != nullptr && found->is_static()) {
			return found;
		}
		if (p_type.script.is_valid()) {
			break; ///script providers having a lazy loader would be REALLY funny
		}
	}
	return nullptr;
}

String GDScriptFunction::describe_generic_instance(Object* p_object) {
	if (p_object == nullptr) {
		return "null";
	}

	ScriptInstance* script_instance = p_object->get_script_instance();
	if (script_instance == nullptr || script_instance->get_language() != GDScriptLanguage::get_singleton()) {
		return p_object->get_class();
	}

	GDScriptInstance* instance = static_cast<GDScriptInstance*>(script_instance);

	String name = GDScript::debug_get_script_name(instance->script);
	if (instance->script.is_valid() && instance->script->generic_param_count > 0) {
		if (instance->type_args.is_empty()) {
			name += "[<erased>]";
		} else {
			name += "[";
			for (int i = 0; i < instance->type_args.size(); i++) {
				if (i > 0) {
					name += ", ";
				}
				name += describe_validator(instance->type_args[i]);
			}
			name += "]";
		}
	}
	return name;
}

void GDScriptPendingTypeArgs::push(const StringName& p_function_name, const Vector<ContainerTypeValidate>& p_args) {
	static constexpr int MAX_ENTRIES = 256;
	if (entries.size() >= MAX_ENTRIES) {
		entries.remove_at(0);
	}
	Entry entry;
	entry.function_name = p_function_name;
	entry.args = p_args;
	entries.push_back(entry);
}

bool GDScriptPendingTypeArgs::take(const StringName& p_function_name, int p_count, Vector<ContainerTypeValidate>& r_args) {
	for (int i = entries.size() - 1; i >= 0; i--) {

		if (entries[i].function_name == p_function_name && entries[i].args.size() == p_count) {
			r_args = entries[i].args;
			entries.resize(i);
			return true;
		}

	}

	return false;
}

GDScriptPendingTypeArgs& GDScriptPendingTypeArgs::get() {
	static thread_local GDScriptPendingTypeArgs pending; ///:sob:
	return pending;
}

bool GDScriptDataType::is_type_exact(const ContainerType &p_container_type) const {
	if (p_container_type.script.is_valid()) {
		return (kind == SCRIPT || kind == GDSCRIPT) && script_type == p_container_type.script.ptr();
	} else if (p_container_type.class_name != StringName()) {
		return kind == NATIVE && native_type == p_container_type.class_name;
	} else {
		return kind == BUILTIN && builtin_type == p_container_type.variant_type;
	}
}

static bool _datatype_matches_validator(const GDScriptDataType& p_type, const ContainerTypeValidate& p_validator) {
	if (p_type.generic_param_index >= 0) {
		return true;
	}

	if (p_validator.variant_type != p_type.builtin_type || p_validator.class_name != p_type.native_type) {
		return false;
	}
	const Script* expected_script = p_type.script_type;
	if (p_validator.script.ptr() != expected_script) {
		return false;
	}

	int expected_nested = 0;
	if (p_type.kind == GDScriptDataType::BUILTIN && p_type.has_container_element_types()) {
		if (p_type.builtin_type == Variant::ARRAY) {
			expected_nested = 1;
		} else if (p_type.builtin_type == Variant::DICTIONARY) {
			expected_nested = 2;
		}
	}

	if (p_validator.nested_types.size() != expected_nested) {
		return false;
	}

	for (int i = 0; i < expected_nested; i++) {
		if (!_datatype_matches_validator(p_type.get_container_element_type_or_variant(i), p_validator.nested_types[i])) {
			return false;
		}
	}
	return true;
}

bool GDScriptDataType::is_type(const Variant &p_variant, bool p_allow_implicit_conversion) const {
	switch (kind) {
		case VARIANT: {
			return true;
		} break;
		case BUILTIN: {
			Variant::Type var_type = p_variant.get_type();
			bool valid = builtin_type == var_type;
			if (valid && builtin_type == Variant::ARRAY && has_container_element_type(0)) {
				Array array = p_variant;
				valid = _datatype_matches_validator(container_element_types[0], array.get_element_validator());
			} else if (valid && builtin_type == Variant::DICTIONARY && has_container_element_types()) {
				Dictionary dictionary = p_variant;
				valid = _datatype_matches_validator(get_container_element_type_or_variant(0), dictionary.get_key_validator()) &&
						_datatype_matches_validator(get_container_element_type_or_variant(1), dictionary.get_value_validator());
			} else if (!valid && p_allow_implicit_conversion) {
				valid = Variant::can_convert_strict(var_type, builtin_type);
			}
			return valid;
		} break;
		case NATIVE: {
			if (p_variant.get_type() == Variant::NIL) {
				return true;
			}
			if (p_variant.get_type() != Variant::OBJECT) {
				return false;
			}

			bool was_freed = false;
			Object *obj = p_variant.get_validated_object_with_check(was_freed);
			if (!obj) {
				return !was_freed;
			}

			if (!obj->is_class(native_type)) {
				return false;
			}
			return true;
		} break;
		case SCRIPT:
		case GDSCRIPT: {
			if (p_variant.get_type() == Variant::NIL) {
				return true;
			}
			if (p_variant.get_type() != Variant::OBJECT) {
				return false;
			}

			bool was_freed = false;
			Object *obj = p_variant.get_validated_object_with_check(was_freed);
			if (!obj) {
				return !was_freed;
			}

			Script *base = obj && obj->get_script_instance() ? obj->get_script_instance()->get_script() : nullptr;
			bool valid = false;
			while (base != nullptr) {
				if (base == script_type) {
					valid = true;
					break;
				}
				base = base->get_base_script().ptr();
			}
			if (valid && has_container_element_types()) {
				const ContainerTypeValidate expected = make_element_validator();
				if (!expected.has_param) {
					valid = GDScriptFunction::generic_instance_matches(obj, expected);
				}
			}
			return valid;
		} break;
	}
	return false;
}

/////////////////////

Variant GDScriptFunction::get_constant(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, constants.size(), "<errconst>");
	return constants[p_idx];
}

StringName GDScriptFunction::get_global_name(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, global_names.size(), "<errgname>");
	return global_names[p_idx];
}

struct _GDFKC {
	int order = 0;
	List<int> pos;
};

struct _GDFKCS {
	int order = 0;
	StringName id;
	int pos = 0;

	bool operator<(const _GDFKCS &p_r) const {
		return order < p_r.order;
	}
};

void GDScriptFunction::debug_get_stack_member_state(int p_line, List<Pair<StringName, int>> *r_stackvars) const {
	int oc = 0;
	HashMap<StringName, _GDFKC> sdmap;
	for (const StackDebug &sd : stack_debug) {
		if (sd.line >= p_line) {
			break;
		}

		if (sd.added) {
			if (!sdmap.has(sd.identifier)) {
				_GDFKC d;
				d.order = oc++;
				d.pos.push_back(sd.pos);
				sdmap[sd.identifier] = d;

			} else {
				sdmap[sd.identifier].pos.push_back(sd.pos);
			}
		} else {
			ERR_CONTINUE(!sdmap.has(sd.identifier));

			sdmap[sd.identifier].pos.pop_back();
			if (sdmap[sd.identifier].pos.is_empty()) {
				sdmap.erase(sd.identifier);
			}
		}
	}

	List<_GDFKCS> stackpositions;
	for (const KeyValue<StringName, _GDFKC> &E : sdmap) {
		_GDFKCS spp;
		spp.id = E.key;
		spp.order = E.value.order;
		spp.pos = E.value.pos.back()->get();
		stackpositions.push_back(spp);
	}

	stackpositions.sort();

	for (_GDFKCS &E : stackpositions) {
		Pair<StringName, int> p;
		p.first = E.id;
		p.second = E.pos;
		r_stackvars->push_back(p);
	}
}

GDScriptFunction::GDScriptFunction() {
	name = "<anonymous>";
#ifdef DEBUG_ENABLED
	{
		MutexLock lock(GDScriptLanguage::get_singleton()->mutex);
		GDScriptLanguage::get_singleton()->function_list.add(&function_list);
		GDScriptLanguage::get_singleton()->unregister_native_class_impl_methods_for_function(this);
	}
#endif
}

GDScriptFunction::~GDScriptFunction() {
	get_script()->member_functions.erase(name);

	///if this function was ALSO registered as a native-type impl method (`impl for int`),
	///scrub it from that registry too, or the VM would happily fuck itself into freed memory
	GDScriptLanguage::get_singleton()->unregister_native_impl_methods_for_function(this);
	GDScriptLanguage::get_singleton()->unregister_native_class_impl_methods_for_function(this);
	GDScriptLanguage::get_singleton()->unregister_script_class_impl_methods_for_function(this);
	GDScriptLanguage::get_singleton()->unregister_enum_impl_methods_for_function(this);

	for (int i = 0; i < lambdas.size(); i++) {
		memdelete(lambdas[i]);
	}

	for (int i = 0; i < argument_types.size(); i++) {
		argument_types.write[i].script_type_ref = Ref<Script>();
	}

	return_type.script_type_ref = Ref<Script>();

#ifdef DEBUG_ENABLED
	MutexLock lock(GDScriptLanguage::get_singleton()->mutex);
	GDScriptLanguage::get_singleton()->function_list.remove(&function_list);
#endif
}

/////////////////////

Variant GDScriptFunctionState::_signal_callback(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	Variant arg;
	r_error.error = Callable::CallError::CALL_OK;

	if (p_argcount == 0) {
		r_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = 1;
		return Variant();
	} else if (p_argcount == 1) {
		//noooneee
	} else if (p_argcount == 2) {
		arg = *p_args[0];
	} else {
		Array extra_args;
		for (int i = 0; i < p_argcount - 1; i++) {
			extra_args.push_back(*p_args[i]);
		}
		arg = extra_args;
	}

	Ref<GDScriptFunctionState> self = *p_args[p_argcount - 1];

	if (self.is_null()) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = p_argcount - 1;
		r_error.expected = Variant::OBJECT;
		return Variant();
	}

	return resume(arg);
}

Variant GDScriptFunctionState::resume(const Variant &p_arg) {
	ERR_FAIL_NULL_V(function, Variant());
	{
		MutexLock lock(GDScriptLanguage::singleton->mutex);

		if (!scripts_list.in_list()) {
#ifdef DEBUG_ENABLED
			ERR_FAIL_V_MSG(Variant(), "Resumed function '" + state.function_name + "()' after await, but script is gone. At script: " + state.script_path + ":" + itos(state.line));
#else
			return Variant();
#endif
		}
		if (state.instance && !instances_list.in_list()) {
#ifdef DEBUG_ENABLED
			ERR_FAIL_V_MSG(Variant(), "Resumed function '" + state.function_name + "()' after await, but class instance is gone. At script: " + state.script_path + ":" + itos(state.line));
#else
			return Variant();
#endif
		}
		// Do these now to avoid locking again after the call.
		scripts_list.remove_from_list();
		instances_list.remove_from_list();
	}

	state.result = p_arg;
	Callable::CallError err;
	Variant ret = function->call(nullptr, nullptr, 0, err, &state);

	function = nullptr; // Cleaned up.
	state.result = Variant();

	return ret;
}

void GDScriptFunctionState::_clear_stack() {
	if (state.stack_size) {
		Variant *stack = (Variant *)state.stack.ptr();
		// First `GDScriptFunction::FIXED_ADDRESSES_MAX` stack addresses are special
		// and not copied to the state, so we skip them here.
		for (int i = GDScriptFunction::FIXED_ADDRESSES_MAX; i < state.stack_size; i++) {
			stack[i].~Variant();
		}
		state.stack_size = 0;
	}
}

void GDScriptFunctionState::_clear_connections() {
	List<Object::Connection> conns;
	get_signals_connected_to_this(&conns);

	for (Object::Connection &c : conns) {
		c.signal.disconnect(c.callable);
	}
}

void GDScriptFunctionState::_bind_methods() {
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "_signal_callback", &GDScriptFunctionState::_signal_callback, MethodInfo("_signal_callback"));

	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::NIL, "result", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
}

GDScriptFunctionState::GDScriptFunctionState() :
		scripts_list(this),
		instances_list(this) {
}

void GDScriptFunctionState::clear() {
	if (cleared) {
		return;
	}
	ERR_FAIL_NULL_MSG(GDScriptLanguage::singleton, "GDScript bug (please report): Function state was not cleared before language shutdown.");
	MutexLock lock(GDScriptLanguage::singleton->mutex);
	if (cleared) {
		return;
	}
	cleared = true;

	_clear_connections();
	scripts_list.remove_from_list();
	instances_list.remove_from_list();
	_clear_stack();
}

GDScriptFunctionState::~GDScriptFunctionState() {
	clear();
}
