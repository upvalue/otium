///// otium:prelude
const print = console.log;

// Builtin operators
const _ot_lt = (a, b) => a < b;
const _ot_sub = (a, b) => a - b;
const _ot_add = (a, b) => a + b;
const _ot_lte = (a, b) => a <= b;
const _ot_gte = (a, b) => a >= b;
const _ot_eq = (a, b) => a === b;
const _ot_neq = (a, b) => a !== b;
const _ot_mul = (a, b) => a * b;
const _ot_div = (a, b) => a / b;

const not = (a) => !a;

///// otium:main

// scratch.ot 4:1
/* r := js("require('raylib')") */
let r, def0;
def0 = require("raylib");
r = def0;
// scratch.ot 6:1
/* screenWidth := 800 */
let screenWidth, def1;
def1 = 800;
screenWidth = def1;
// scratch.ot 7:1
/* screenHeight := 600 */
let screenHeight, def2;
def2 = 600;
screenHeight = def2;
// scratch.ot 9:1
/* ballPos := r.Vector2(400, 300) */
let ballPos, def3;
let apply_operator4, apply_operand5_0, apply_operand6_1;
apply_operator4 = r.Vector2;
apply_operand5_0 = 400;
apply_operand6_1 = 300;

def3 = apply_operator4(apply_operand5_0, apply_operand6_1);
ballPos = def3;
// scratch.ot 11:1
/* r.InitWindow(screenWidth, screenHeight, "raylib ball movement demo in otium") */
let apply_operator7, apply_operand8_0, apply_operand9_1, apply_operand10_2;
apply_operator7 = r.InitWindow;
apply_operand8_0 = screenWidth;
apply_operand9_1 = screenHeight;
apply_operand10_2 = "raylib ball movement demo in otium";

apply_operator7(apply_operand8_0, apply_operand9_1, apply_operand10_2);
// scratch.ot 12:1
/* r.SetTargetFPS(60) */
let apply_operator11, apply_operand12_0;
apply_operator11 = r.SetTargetFPS;
apply_operand12_0 = 60;

apply_operator11(apply_operand12_0);
// scratch.ot 14:1
/* while(not(r.WindowShouldClose())) {
  r.BeginDrawing()

  if(r.IsKeyDown(r.KEY_RIGHT), ballPos.x = ballPos.x + 2)
  if(r.IsKeyDown(r.KEY_LEFT), ballPos.x = ballPos.x - 2)
  if(r.IsKeyDown(r.KEY_UP), ballPos.y = ballPos.y - 2)
  if(r.IsKeyDown(r.KEY_DOWN), ballPos.y = ballPos.y + 2)

  r.ClearBackground(r.RAYWHITE)
  r.DrawText("move the ball with arrow keys", 10, 10, 20, r.DARKGRAY)
  r.DrawCircleV(ballPos, 50, r.MAROON)

  r.EndDrawing()
} */
let while_cond13;
let apply_operator15, apply_operand16_0;
apply_operator15 = not;
let apply_operator17;
apply_operator17 = r.WindowShouldClose;

apply_operand16_0 = apply_operator17();

while_cond13 = apply_operator15(apply_operand16_0);
while (while_cond13 !== false) {
  let apply_operator18;
  apply_operator18 = r.BeginDrawing;

  apply_operator18();
  let if_cond19;
  let apply_operator20, apply_operand21_0;
  apply_operator20 = r.IsKeyDown;
  apply_operand21_0 = r.KEY_RIGHT;

  if_cond19 = apply_operator20(apply_operand21_0);
  if (if_cond19 !== false) {
    let apply_operator23, apply_operand24_0, apply_operand25_1;
    apply_operator23 = _ot_add;
    apply_operand24_0 = ballPos.x;
    apply_operand25_1 = 2;

    set22 = apply_operator23(apply_operand24_0, apply_operand25_1);
    ballPos.x = set22;
  }
  let if_cond26;
  let apply_operator27, apply_operand28_0;
  apply_operator27 = r.IsKeyDown;
  apply_operand28_0 = r.KEY_LEFT;

  if_cond26 = apply_operator27(apply_operand28_0);
  if (if_cond26 !== false) {
    let apply_operator30, apply_operand31_0, apply_operand32_1;
    apply_operator30 = _ot_sub;
    apply_operand31_0 = ballPos.x;
    apply_operand32_1 = 2;

    set29 = apply_operator30(apply_operand31_0, apply_operand32_1);
    ballPos.x = set29;
  }
  let if_cond33;
  let apply_operator34, apply_operand35_0;
  apply_operator34 = r.IsKeyDown;
  apply_operand35_0 = r.KEY_UP;

  if_cond33 = apply_operator34(apply_operand35_0);
  if (if_cond33 !== false) {
    let apply_operator37, apply_operand38_0, apply_operand39_1;
    apply_operator37 = _ot_sub;
    apply_operand38_0 = ballPos.y;
    apply_operand39_1 = 2;

    set36 = apply_operator37(apply_operand38_0, apply_operand39_1);
    ballPos.y = set36;
  }
  let if_cond40;
  let apply_operator41, apply_operand42_0;
  apply_operator41 = r.IsKeyDown;
  apply_operand42_0 = r.KEY_DOWN;

  if_cond40 = apply_operator41(apply_operand42_0);
  if (if_cond40 !== false) {
    let apply_operator44, apply_operand45_0, apply_operand46_1;
    apply_operator44 = _ot_add;
    apply_operand45_0 = ballPos.y;
    apply_operand46_1 = 2;

    set43 = apply_operator44(apply_operand45_0, apply_operand46_1);
    ballPos.y = set43;
  }
  let apply_operator47, apply_operand48_0;
  apply_operator47 = r.ClearBackground;
  apply_operand48_0 = r.RAYWHITE;

  apply_operator47(apply_operand48_0);
  let apply_operator49,
    apply_operand50_0,
    apply_operand51_1,
    apply_operand52_2,
    apply_operand53_3,
    apply_operand54_4;
  apply_operator49 = r.DrawText;
  apply_operand50_0 = "move the ball with arrow keys";
  apply_operand51_1 = 10;
  apply_operand52_2 = 10;
  apply_operand53_3 = 20;
  apply_operand54_4 = r.DARKGRAY;

  apply_operator49(
    apply_operand50_0,
    apply_operand51_1,
    apply_operand52_2,
    apply_operand53_3,
    apply_operand54_4,
  );
  let apply_operator55, apply_operand56_0, apply_operand57_1, apply_operand58_2;
  apply_operator55 = r.DrawCircleV;
  apply_operand56_0 = ballPos;
  apply_operand57_1 = 50;
  apply_operand58_2 = r.MAROON;

  apply_operator55(apply_operand56_0, apply_operand57_1, apply_operand58_2);
  let apply_operator59;
  apply_operator59 = r.EndDrawing;

  while_body14 = apply_operator59();
  let apply_operator60, apply_operand61_0;
  apply_operator60 = not;
  let apply_operator62;
  apply_operator62 = r.WindowShouldClose;

  apply_operand61_0 = apply_operator62();

  while_cond13 = apply_operator60(apply_operand61_0);
}
// scratch.ot 29:1
/* r.CloseWindow() */
let apply_operator63;
apply_operator63 = r.CloseWindow;

apply_operator63();
// scratch.ot 31:1
/*  */

