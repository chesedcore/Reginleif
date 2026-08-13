trait RuntimeTraitTestInt

func rt_min(other: int) -> int {
	return mini(self, other)
}

func rt_add_all(first: int = 0, ...rest: Array) -> int {
	var total := self + first
	for value: int in rest:
		total += value
	return total
}

func rt_callable_value() -> int {
	return self * 2
}

func rt_times(other: int) -> int

impl for int {
	func rt_times(other: int) -> int {
		return self * other
	}
}
