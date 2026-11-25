#pragma once

#include "core/string/string_name.h"
#include "core/templates/vector.h"

struct GenericMethodInfo {
	Vector<StringName> params;
	Vector<StringName> bounds;
	int return_param = -1;
};

namespace GenericMethods {
void register_method(const StringName& p_class, const StringName& p_method, const GenericMethodInfo& p_info);
const GenericMethodInfo* find(const StringName& p_class, const StringName& p_method);
void clear();
} //namespace GenericMethods