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

void GenericMethods::clear() {
	_registry().clear();
}