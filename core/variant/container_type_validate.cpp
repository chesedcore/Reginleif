/**************************************************************************/
/*  container_type_validate.cpp                                           */
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

#include "core/variant/container_type_validate.h"

const Variant *ContainerTypeValidate::_internal_convert_variant(const Variant &p_variant, Variant &r_tmp_variant, const char *p_operation, bool p_output_errors) const {
	if (Variant::can_convert_strict(p_variant.get_type(), variant_type)) {
		const Variant *converted_from = &p_variant;
		Callable::CallError call_error;
		Variant::construct(variant_type, r_tmp_variant, &converted_from, 1, call_error);

		if (call_error.error == Callable::CallError::CALL_OK) {
			return &r_tmp_variant;
		}
	}

	if (p_output_errors) {
		ERR_FAIL_V_MSG(nullptr, vformat("Attempted to %s a variable of type '%s' into a %s of incompatible type '%s'.", String(p_operation), Variant::get_type_name(p_variant.get_type()), where, Variant::get_type_name(variant_type)));
	} else {
		return nullptr;
	}
}

bool ContainerTypeValidate::_internal_validate_object(const Variant &p_variant, const char *p_operation, bool p_output_errors) const {
	ERR_FAIL_COND_V(p_variant.get_type() != Variant::OBJECT, false);

#ifdef DEBUG_ENABLED
	ObjectID object_id = p_variant;
	if (object_id == ObjectID()) {
		return true; // This is fine, it's null.
	}
	Object *object = ObjectDB::get_instance(object_id);
	if (object == nullptr) {
		if (p_output_errors) {
			ERR_FAIL_V_MSG(false, vformat("Attempted to %s an invalid (previously freed?) object instance into a '%s'.", String(p_operation), String(where)));
		} else {
			return false;
		}
	}
#else
	Object *object = p_variant;
	if (object == nullptr) {
		return true; //fine
	}
#endif
	if (class_name == StringName()) {
		return true; // All good, no class type requested.
	}

	const StringName &obj_class = object->get_class_name();
	if (obj_class != class_name && !object->is_class(class_name)) {
		if (p_output_errors) {
			String object_class_name = object->get_class();
			if (ScriptInstance *script_instance = object->get_script_instance(); script_instance != nullptr) {
				if (Script *other_script = script_instance->get_script(); other_script != nullptr) {
					if (const StringName &script_global_name = other_script->get_global_name(); !script_global_name.is_empty()) {
						object_class_name = script_global_name;
					}
				}
			}
			ERR_FAIL_V_MSG(false, vformat("Attempted to %s an object of type '%s' into a %s of incompatible type '%s'.", String(p_operation), object_class_name, where, String(class_name)));
		} else {
			return false;
		}
	}

	if (script.is_null()) {
		return true; // All good, no script requested.
	}

	Script *other_script = object->get_script_instance() != nullptr ? object->get_script_instance()->get_script() : nullptr;

	// Check base script..
	if (other_script == nullptr) {
		if (p_output_errors) {
			ERR_FAIL_V_MSG(false, vformat("Attempted to %s an object into a %s of incompatible type '%s'.", String(p_operation), String(where), String(script->get_class_name())));
		} else {
			return false;
		}
	}
	if (!other_script->inherits_script(script)) {
		if (p_output_errors) {
			ERR_FAIL_V_MSG(false, vformat("Attempted to %s an object into a %s of incompatible type '%s'.", String(p_operation), String(where), String(script->get_class_name())));
		} else {
			return false;
		}
	}

	return true;
}

///warning! don't try to fix this to 'copy on write'
const Variant* ContainerTypeValidate::_validate_nested(const Variant* p_validated, Variant& r_tmp_variant, const char* p_operation, bool p_output_errors) const {
	if (nested_types.is_empty()) {
		return p_validated;
	}

	if (variant_type == Variant::ARRAY && p_validated->get_type() == Variant::ARRAY) {
		Array arr = *p_validated;
		const ContainerTypeValidate& elem_type = nested_types[0];
		for (int i = 0; i < arr.size(); i++) {
			Variant elem_tmp;
			const Variant* elem = elem_type._validate_full(arr[i], elem_tmp, p_operation, p_output_errors);
			if (elem == nullptr) {
				return nullptr;
			}
			if (elem == &elem_tmp) {
				arr[i] = *elem;
			}
		}
		if (p_validated == &r_tmp_variant) {
			r_tmp_variant = arr;
		}
		return p_validated;
	}

	if (variant_type == Variant::DICTIONARY && nested_types.size() >= 2 && p_validated->get_type() == Variant::DICTIONARY) {
		Dictionary dict = *p_validated;
		const ContainerTypeValidate& key_type = nested_types[0];
		const ContainerTypeValidate& value_type = nested_types[1];
		const Array keys = dict.keys();
		for (int i = 0; i < keys.size(); i++) {
			Variant old_key = keys[i];
			Variant key_tmp;
			Variant value_tmp;
			const Variant* new_key = key_type._validate_full(old_key, key_tmp, p_operation, p_output_errors);
			if (new_key == nullptr) {
				return nullptr;
			}
			const Variant* new_value = value_type._validate_full(dict[old_key], value_tmp, p_operation, p_output_errors);
			if (new_value == nullptr) {
				return nullptr;
			}
			if (new_key == &key_tmp || new_value == &value_tmp) {
				if (new_key == &key_tmp) {
					dict.erase(old_key);
				}
				dict[*new_key] = *new_value;
			}
		}
		if (p_validated == &r_tmp_variant) {
			r_tmp_variant = dict;
		}
		return p_validated;
	}

	return p_validated;
}

ContainerTypeValidate ContainerTypeValidate::resolved(const ContainerTypeValidate* p_args, int p_count) const {
	if (!has_param) {
		return *this;
	}

	if (param_index >= 0) {
		if (p_args != nullptr && param_index < p_count) {
			return p_args[param_index];
		}
		return ContainerTypeValidate(); ///boom >.<
	}

	ContainerTypeValidate result = *this;
	result.has_param = false;
	for (int i = 0; i < result.nested_types.size(); i++) {
		result.nested_types.write[i] = nested_types[i].resolved(p_args, p_count);
	}
	
	return result;
}
