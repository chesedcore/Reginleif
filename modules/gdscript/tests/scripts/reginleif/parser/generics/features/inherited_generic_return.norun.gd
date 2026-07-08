class_name InheritedGenericReturnType

class Base[T]:
    var value: T

    func get_value() -> T:
        return value

class Intermediate[U] extends Base[U]:
    pass

class Derived extends Intermediate[int]:
    pass

func use_inherited_return_type(derived: Derived) -> void:
    var _value: int = derived.get_value()
