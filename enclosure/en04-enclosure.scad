// EE04 snap-on frame
// =====================================================================
// Lightweight snap-fit bezel for the Seeed XIAO ePaper Display Board
// EE04. Open back, open center window, cantilever snap clips that grip
// the board's back edge. No bottom layer.
//
// Board outline + thickness taken from the official KiCad PCB
// (XIAO ePaper Display Board EE04_V1.2):
//   outline 41.23 x 80.00 mm, 1.6 mm thick.
//
// Print orientation: bezel face DOWN on the bed, clips pointing up (+Z).
// Assemble by pressing the board in from the open back until the four
// corner clips snap over its rear face.
//
// Cutouts: a test print showed the thin rim clears the buttons, the
// display ribbon and the battery JST without help — only the USB-C port
// (on the XIAO, near the top of the LEFT edge) actually needs an opening.
// The button/ribbon/battery positions are kept commented in port_cuts in
// case a future panel or board revision fouls the rim.
// =====================================================================

/* ---- board (from PCB) ---------------------------------------------- */
board_w = 41.23; // X
board_h = 80.00; // Y
board_t = 1.60; // thickness

/* ---- fit / frame --------------------------------------------------- */
fit = 0.35; // clearance per side around the board/home/bob/documents2/Arduino/einkdisplay/images/
fit_long = 0.3; // extra clearance per side on the long (80 mm) axis
t_play = 0.3; // extra thickness room — the hook catches this much proud
wall = 2.0; // side-wall thickness
bezel = 2.0; // front lip overlapping the board face (inward)
bezel_t = 0.6; // front lip thickness — kept minimal (just a forward stop)
corner_r = 3.0; // outer corner radius
eps = 0.01;
bottom_h = 1.6;

/* ---- snap clips ---------------------------------------------------- */
clip_w = 11.0; // clip finger width (along the wall) — wide for a firm grab
clip_t = 0.8; // clip finger thickness (radial) — thin so it flexes, not cracks
slot_w = 0.6; // gap each side of the finger (sets the cantilever)
hook_reach = 1.1; // how far the hook overlaps the board's back face (the grab)
hook_h = 2.2; // hook height above board back — tall so the finger stays compliant

/* ---- preview ------------------------------------------------------- */
show_board = true; // ghost the board to check fit; turn off to export

/* ---- derived ------------------------------------------------------- */
pocket_w = board_w + 2 * fit;
pocket_h = board_h + 2 * (fit + fit_long);
outer_w = pocket_w + 2 * wall;
outer_h = pocket_h + 2 * wall;
wall_h_hook = bezel_t + board_t + t_play;
extra_h = 1;
wall_h = extra_h + bezel_t + board_t + t_play; // hook catches t_play proud of the board back
win_w = pocket_w - 2 * bezel; // front window opening
win_h = pocket_h - 2 * bezel;
$fn = 48;

// Clip placements: [z-rotation, inner-face distance, offset along wall, width].
// Short (top/bottom) walls only — the long edges are lined with components,
// so no clips there. The top-right clip is narrowed and shifted to slot
// between the battery JST (ends at x≈31.6) and the corner.
clips = [
  [0, pocket_h / 2, -13.5, clip_w], // top-left
  [0, pocket_h / 2, 16.0, 6.0], // top-right (clears battery JST)
  [180, pocket_h / 2, -13.5, clip_w], // bottom-left
  [180, pocket_h / 2, 13.5, clip_w], // bottom-right
];

// Edge cutouts: [z-rotation, inner-face distance, offset along wall, width]
// Offsets derived from PCB part positions (board portrait, origin top-left:
// LEFT offset = 40 - y, RIGHT offset = y - 40, TOP offset = x - 20.6).
port_cuts = [
  // LEFT edge (rot 90): USB-C — the only opening the thin rim needs
  [90, pocket_w / 2, 26.0, 11], // USB-C   (XIAO)
  // Everything below clears the rim without a cutout (verified on a print).
  // Re-enable only if a board/panel revision fouls the edge:
  // [  90, pocket_w / 2,   8.4,  6], // user key (SW3)
  // [  90, pocket_w / 2,  -0.9,  6], // user key (SW4)
  // [  90, pocket_w / 2, -10.3,  6], // user key (SW5)
  // [  90, pocket_w / 2, -19.6,  6], // reset    (SW2)
  // [  90, pocket_w / 2, -29.7,  7], // power sw (SW1)
  // [ -90, pocket_w / 2,  14.5, 33], // 50-pin FPC ribbon (J1)
  // [   0, pocket_h / 2,   7.4,  8], // battery JST (J3)
];

module rrect(w, h, r) {
  offset(r=r) offset(r=-r) square([w, h], center=true);
}

module bottom() {
  linear_extrude(bottom_h) rrect(outer_w, outer_h, corner_r);
}

module frame_body() {
  difference() {
    linear_extrude(wall_h) rrect(outer_w, outer_h, corner_r);
    // board pocket, open at the back
    translate([0, 0, bezel_t])
      linear_extrude(wall_h) rrect(pocket_w, pocket_h, 1.0);
    // front window, open center
    translate([0, 0, -eps])
      linear_extrude(bezel_t + 2 * eps) rrect(win_w, win_h, 1.0);
  }
}

// recess carved into the wall so the finger can flex outward
module clip_window(inner, w) {
  translate([-(w + 2 * slot_w) / 2, inner - eps, bezel_t])
    cube(
      [
        w + 2 * slot_w,
        wall + 2 * eps,
        wall_h_hook + 1 + hook_h - bezel_t + eps,
      ]
    );
}

// the cantilever finger + retaining hook
module clip_finger(inner, w) {
  // beam up to the board back
  translate([-w / 2, inner - eps, bezel_t])
    cube([w, clip_t + eps, wall_h_hook - bezel_t]);
  // hook: catching face on the bottom, lead-in slope on top
  hull() {
    for (
      p = [
        [inner + clip_t, wall_h_hook],
        [inner + clip_t, wall_h_hook + hook_h],
        [inner - hook_reach, wall_h_hook],
      ]
    )
      translate([0, p[0], p[1]]) cube([w, eps, eps], center=true);
  }
}

module place(c) rotate([0, 0, c[0]]) translate([c[2], 0, 0]) children();

module ee04_frame() {
  difference() {
    frame_body();
    for (c = clips) place(c) clip_window(c[1], c[3]);
    for (p = port_cuts)
      rotate([0, 0, p[0]]) translate([p[2], 0, 0])
          translate([-p[3] / 2, p[1] - eps, -eps])
            cube([p[3], wall + 2 * eps, wall_h + 2 * eps]);
  }
  for (c = clips) place(c) clip_finger(c[1], c[3]);
}

module IOCutouts() {
  big = 1e3;
  intersection() {
    cube(big);

    translate([-86, 116.86 / 2, 0])
      rotate([-90, 0, 0])
        import("display outer enclosure.stl");
  }
}

// CODE
bottom();

import("board.stl");

translate([-0.5 * board_w, 16 + 0.5 * board_h, 0])
  rotate([0, 0, 180])
    IOCutouts();

translate([0, 0, bottom_h]) {
  ee04_frame();

  // if (show_board) {
  //translate([0, 0, bezel_t])

  // linear_extrude(board_t) rrect(board_w, board_h, 3.0);
  // }
}
