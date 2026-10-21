--!strict
-- Button swap: two of the controller's buttons trade places before the game reads them
-- (README.md). One callback, on each pad report; nothing else of the game is touched.

-- The report as the port's pad library gives it (port/game/common/lib/pad.c, scePad2Read), 18 bytes:
--   0-1   the buttons, a 16-bit word, little-endian, a bit clear for a button held
--   2-5   the right stick (x, y), then the left (x, y), 0x80 at rest
--   6-17  each button's pressure, 0xFF held and 0 not: right, left, up, down, triangle, circle,
--         cross, square, L1, R1, L2, R2
-- So a button is a bit of the word and a pressure byte, and a swap moves both.
type Button = { bit: number, pressure: number }

local BUTTONS: { [string]: Button } = {
	triangle = { bit = 12, pressure = 10 },
	circle = { bit = 13, pressure = 11 },
	cross = { bit = 14, pressure = 12 },
	square = { bit = 15, pressure = 13 },
	l1 = { bit = 10, pressure = 14 },
	r1 = { bit = 11, pressure = 15 },
}

local PAIRS: { [string]: { string } } = {
	["cross-square"] = { "cross", "square" },
	["circle-triangle"] = { "circle", "triangle" },
	["l1-r1"] = { "l1", "r1" },
}

local REPORT_SIZE = 18

local chosen = PAIRS[openrac.setting("pair") :: string] or PAIRS["cross-square"]
local a, b = BUTTONS[chosen[1]], BUTTONS[chosen[2]]

openrac.on_pad(function(_port: number, report: buffer)
	if buffer.len(report) < REPORT_SIZE then
		return
	end
	local word = buffer.readu16(report, 0)
	local held_a, held_b = bit32.extract(word, a.bit), bit32.extract(word, b.bit)
	word = bit32.replace(word, held_b, a.bit)
	word = bit32.replace(word, held_a, b.bit)
	buffer.writeu16(report, 0, word)
	local pressure_a, pressure_b = buffer.readu8(report, a.pressure), buffer.readu8(report, b.pressure)
	buffer.writeu8(report, a.pressure, pressure_b)
	buffer.writeu8(report, b.pressure, pressure_a)
end)

openrac.log(`swapping {chosen[1]} and {chosen[2]}`)
