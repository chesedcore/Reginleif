#include "generic_method.h"

#include "core/object/class_db.h"
#include "core/templates/hash_map.h"

static HashMap<StringName, HashMap<StringName, GenericMethodInfo>>& _registry() {
	static HashMap<StringName, HashMap<StringName, GenericMethodInfo>> registry;
	return registry;
}

void GenericMethods::register_method(const StringName& p_class, const StringName& p_method, const GenericMethodInfo& p_info) {
	_registry()[p_class][p_method] = p_info;
}

const GenericMethodInfo* GenericMethods::find(const StringName& p_class, const StringName& p_method) {
	for (StringName cls = p_class; cls != StringName(); cls = ClassDB::get_parent_class(cls)) {
		HashMap<StringName, HashMap<StringName, GenericMethodInfo>>::Iterator c = _registry().find(cls);
		if (!c) {
			continue;
		}
		HashMap<StringName, GenericMethodInfo>::Iterator m = c->value.find(p_method);
		if (m) {
			return &m->value;
		}
	}
	return nullptr;
}

GenericTypeToken GenericTypeToken::decode(const Variant& p_token) {
	GenericTypeToken t;
	switch (p_token.get_type()) {
		case Variant::OBJECT: {
			t.type = Variant::OBJECT;
			t.script = p_token;
		} break;
		case Variant::STRING_NAME: {
			t.type = Variant::OBJECT;
			t.class_name = p_token;
		} break;
		case Variant::INT: {
			t.type = (Variant::Type)(int64_t)p_token;
		} break;
		case Variant::ARRAY: {
			const Array a = p_token;
			if (a.is_empty()) {
				break;
			}
			t.type = (Variant::Type)(int64_t)a[0];
			for (int i = 1; i < a.size(); i++) {
				t.nested.push_back(decode(a[i]));
			}
		} break;
		default: {
		} break; ///nil is untyped
	}
	return t;
}

void GenericMethods::clear() {
	_registry().clear();
}
