// raylib-ball.js - move the ball with arrow keys

const r = require('raylib');

const screenWidth = 800, screenHeight = 600;

r.InitWindow(screenWidth, screenHeight, "raylib ball demo")
r.SetTargetFPS(60)

while (!r.WindowShouldClose()) {
    r.BeginDrawing();
    r.ClearBackground(r.RAYWHITE)
    r.DrawText("Congrats! You created your first node-raylib window!", 120, 200, 20, r.LIGHTGRAY)
    r.EndDrawing()
}
r.CloseWindow()
