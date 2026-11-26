#pragma once

#include "core/object/ref_counted.h"
#include "core/object/script_language.h"
#include "core/string/string_name.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

struct GenericTypeDesc {
	enum Kind {
		UNSET,
		GENERIC,
		VARIANT_TYPE,
		VOID_TYPE,
		BUILTIN,
		NATIVE,
		ARRAY,
		DICTIONARY,
		CALLABLE,
	};

	Kind kind = UNSET;
	StringName name;
	Variant::Type builtin = Variant::NIL;
	Vector<GenericTypeDesc> elems;
	Vector<GenericTypeDesc> ret;

	static GenericTypeDesc generic(const StringName& p_name) {
		GenericTypeDesc d;
		d.kind = GENERIC;
		d.name = p_name;
		return d;
	}
	static GenericTypeDesc native(const StringName& p_class) {
		GenericTypeDesc d;
		d.kind = NATIVE;
		d.name = p_class;
		return d;
	}
	static GenericTypeDesc builtin_type(Variant::Type p_type) {
		GenericTypeDesc d;
		d.kind = BUILTIN;
		d.builtin = p_type;
		return d;
	}
	static GenericTypeDesc variant() {
		GenericTypeDesc d;
		d.kind = VARIANT_TYPE;
		return d;
	}
	static GenericTypeDesc void_type() {
		GenericTypeDesc d;
		d.kind = VOID_TYPE;
		return d;
	}
	static GenericTypeDesc array(const GenericTypeDesc& p_elem) {
		GenericTypeDesc d;
		d.kind = ARRAY;
		d.elems.push_back(p_elem);
		return d;
	}
	static GenericTypeDesc dictionary(const GenericTypeDesc& p_key, const GenericTypeDesc& p_value) {
		GenericTypeDesc d;
		d.kind = DICTIONARY;
		d.elems.push_back(p_key);
		d.elems.push_back(p_value);
		return d;
	}
	static GenericTypeDesc callable(const Vector<GenericTypeDesc>& p_params, const GenericTypeDesc& p_return) {
		GenericTypeDesc d;
		d.kind = CALLABLE;
		d.elems = p_params;
		d.ret.push_back(p_return);
		return d;
	}
};

struct GenericMethodInfo {
	Vector<StringName> params;
	Vector<StringName> bounds;
	Vector<GenericTypeDesc> arg_types; ///if unset keep the native methodinfotype
	GenericTypeDesc return_type;
	int return_param = -1;
};

struct GenericTypeToken {
	Variant::Type type = Variant::NIL;
	StringName class_name;
	Ref<Script> script; ///type==OBJECT and bound to a scriptclass
	Vector<GenericTypeToken> nested;

	static GenericTypeToken decode(const Variant& p_token);
};

namespace GenericMethods {
void register_method(const StringName& p_class, const StringName& p_method, const GenericMethodInfo& p_info);
const GenericMethodInfo* find(const StringName& p_class, const StringName& p_method);
void clear();
} //namespace GenericMethods
