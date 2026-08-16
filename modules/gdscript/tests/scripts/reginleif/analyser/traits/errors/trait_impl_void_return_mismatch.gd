trait VoidReturnMismatch

func test() -> void

impl for int {
	func test() -> int { return self }
}
