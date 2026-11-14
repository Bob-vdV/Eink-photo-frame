// EE04 closed enclosure: frame with SD-card bay + lid (slide-in or screw-on)
// =====================================================================
// Seeed XIAO ePaper Display Board EE04 (PCB 41.23 x 80.00 x 1.6 mm)
// + microSD adapter module (TinyTronics 000375: 42 x 24 x 12 mm listed).
//
// Layout (top view, +Y up):
//   main pocket   EE04 board, USB-C / buttons / power switch on the LEFT wall
//   SD bay        below the board; module lies flat, card slot faces the LEFT
//                 wall (same side as the power switch), header pins to the right
//
// Assembly orientation (+Z up):
//   closed floor, board on a 1 mm standoff ring, lid on top.
//   lid_mode "slide": lid slides in along grooves from the bottom (-Y) end
//   lid_mode "screw": flat plate + 4 corner M2 screws
//
// Print: part = frame (floor down), part = lid (outer face down).
// Preview: part = assembly, show_board / show_sd for ghost parts.
// In OpenSCAD use Window > Customizer to pick options from dropdowns.
// =====================================================================

/* [Main] */
// Which part to show / export
part = "assembly"; // [assembly:Assembly preview, frame:Frame (print this), lid:Lid (print this), both:Frame and lid side by side]
// Lid style
lid_mode = "slide"; // [slide:Slide-in lid (no screws), screw:Screw-on lid (4x M2)]
// Ghost of board.stl in the assembly preview (needs board.stl next to this file)
show_board = false;
// Ghost block of the SD module in the assembly preview
show_sd = false;
// Snap clips that grip the board's back edge
use_clips = true;

/* [EE04 board] */
// PCB width (X)
board_w = 41.23; // [30:0.01:60]
// PCB length (Y)
board_h = 80.00; // [60:0.01:100]
// PCB thickness
board_t = 1.60; // [1.0:0.1:2.4]
// Tallest part above the PCB top face (7.095 - 1.6 in board.stl)
comp_h = 5.50; // [3:0.1:9]
// Air between the tallest part and the lid
comp_clear = 0.80; // [0.5:0.1:2]

/* [SD module] */
// Add the SD module bay below the board (off = board-only enclosure)
sd_bay = true;
// Module PCB length (listing: 42)
sd_len = 42.0; // [30:0.1:60]
// Module PCB width (listing: 24)
sd_w = 24.0; // [15:0.1:40]
// Module PCB thickness (assumed)
sd_pcb_t = 1.2; // [0.8:0.1:2]
// Header pins sticking out past the PCB edge (from the photo)
sd_pin = 4.5; // [0:0.5:12]
// Module underside height above the floor
sd_stand = 1.5; // [0.5:0.1:4]
// Clearance around the module
sd_gap = 0.5; // [0.2:0.1:1.5]
// Card opening width in the left wall (socket is ~12 mm wide)
sd_slot_w = 14.0; // [10:0.5:20]
// Wall between board pocket and SD bay (kept low so wires pass over)
div = 2.0; // [1.2:0.1:4]
// Add locating pins for the module's two left mounting holes (assumed 2.5 mm from the edges)
sd_pins = false;

/* [Frame] */
// Clearance per side around the board (X)
fit = 0.35; // [0.1:0.05:1]
// Extra clearance per side on the long axis (Y)
fit_long = 0.30; // [0:0.05:1]
// Wall thickness
wall = 2.0; // [1.2:0.1:4]
// Outer corner radius
corner_r = 3.0; // [1:0.5:8]
// Inside corner radius of the board pocket (0 = square corners)
pocket_r = 0; // [0:0.25:2]
// Closed floor thickness
bottom_h = 1.6; // [1:0.1:3]
// PCB standoff height (parts on the PCB underside reach 1.93 mm)
bottom_clear = 2.2; // [2.0:0.1:4]
// Width of the standoff ring under the PCB edge
ledge_w = 1.0; // [0.5:0.1:2]

/* [Snap clips] */
// Hook catches this much proud of the PCB back face
t_play = 0.3; // [0:0.05:1]
// Finger width along the wall (the +Y end clips use their own narrower width)
clip_w = 11.0; // [4:0.5:16]
// Finger thickness (thin so it flexes)
clip_t = 0.8; // [0.5:0.1:1.5]
// Gap each side of the finger
slot_w = 0.6; // [0.3:0.1:1.2]
// How far the hook overlaps the board's back face
hook_reach = 1.1; // [0.6:0.1:2]
// Hook height above the board back
hook_h = 2.2; // [1:0.1:4]

/* [SD module clips] */
// Snap clips that hold the SD module down (2 at the card-slot end, 2 on the far edge)
use_sd_clips = true;
// Hook overlap on the module PCB (keep above sd_gap - 0.3)
sd_hook_reach = 0.9; // [0.6:0.1:1.5]
// Hook height (kept low so the lid end cap slides over it)
sd_hook_h = 0.85; // [0.6:0.05:1.0]

/* [Slide lid] */
// Sliding clearance (raise to 0.2-0.25 if it binds)
gap = 0.15; // [0.05:0.01:0.4]
// Thickness of the lid tongue tip (the tongue slopes 45 degrees so it prints without supports)
tongue_tip = 0.8; // [0.6:0.1:1.5]
// Groove depth into the long walls
gd = 1.0; // [0.6:0.1:1.5]
// Wall material above the groove
rail_t = 1.25; // [0.8:0.05:2.5]
// Detent bump height (0 = no detent)
bump_h = 0.35; // [0:0.05:0.8]
// Detent bump radius
bump_r = 0.9; // [0.5:0.1:1.5]

/* [Screw lid] */
// Lid plate thickness
lid_t = 2.0; // [1.2:0.1:4]
// Corner screw boss radius
ear_r = 2.7; // [2:0.1:4]
// Boss centre offset beyond the pocket corner
ear_off = 1.3; // [0.5:0.1:3]
// Pilot hole in the frame (M2 self-tapping)
screw_pilot = 1.8; // [1.4:0.1:2.5]
// Clearance hole in the lid
screw_clear = 2.4; // [2:0.1:3.5]
// Countersink diameter
screw_head = 4.4; // [3:0.1:6]
// Pilot hole depth
screw_depth = 7.0; // [3:0.5:10]
// Corner pads that press the board down (screw lid only; a sliding lid would drag them over the parts)
slide_pads = false;
// Pad size
pad = 2.4; // [1:0.1:4]
// Pad position X
pad_x = 17.4; // [10:0.1:20]
// Pad position Y
pad_y = 37.6; // [30:0.1:40]
// Pads stop this far short of the PCB
pad_relief = 0.1; // [0:0.05:0.5]

/* [Left wall openings] */
// USB-C opening: rounded rectangle, sized for a plug overmould (the original enclosure used 9.3 x 3.7)
usb_y = 25.65; // [10:0.05:40]
usb_z = 4.25; // [1:0.05:8]
usb_w = 12.0; // [8:0.1:16]
usb_h = 5.5; // [3:0.1:8]
usb_r = 1.5; // [0.5:0.1:2.75]
// Power slide switch opening: rounded slot (the original enclosure used 6.3 x 4.0)
sw_y = -28.7; // [-40:0.05:-15]
sw_z = 3.8; // [1:0.05:8]
sw_w = 8.0; // [5:0.1:14]
sw_h = 4.0; // [2.5:0.1:7]
sw_r = 2.0; // [0.5:0.1:3.5]
// Button covers: U-slot flex tab with a raised press pad (off = plain rectangular holes)
btn_covers = true;
// Button positions along the wall (Y)
btn_y = [8.4, -0.9, -10.3, -19.6]; // [-40:0.1:40]
// Height of the button centre above the PCB underside
btn_z = 3.85; // [1:0.05:8]
// Tab width
btn_tab_w = 5.4; // [4:0.1:7]
// Width of the U slot around the tab
btn_slot = 0.7; // [0.4:0.05:1.2]
// Tab length from hinge to free end
btn_tab_len = 7.9; // [5:0.1:10]
// Press pad height above the wall
btn_pad_h = 1.0; // [0.5:0.1:2]
// Recess on the tab's inner face so the button tip does not sit against it
btn_recess = 0.25; // [0.1:0.05:0.6]
// Thin flexure at the hinge so the tab is easy to press
btn_relief = true;
btn_relief_depth = 0.9; // [0.4:0.1:1.4]
btn_relief_len = 2.0; // [1:0.1:4]

/* [Openings] */
// 50-pin FPC connector centre Y (connector spans y -29.5 .. -1), right wall
fpc_y = -15.25; // [-40:0.25:40]
// FPC slot width (ribbon is ~27 mm)
fpc_w = 31.0; // [20:0.5:45]
// Second smaller connector at y 2 .. 17.5 on the right edge
fpc2_cutout = false;
// JST battery cable exit in the top wall
battery_cutout = false;
// Battery opening position X
battery_x = 7.4; // [-18:0.1:18]
// Battery opening width
battery_w = 8; // [4:0.5:16]

/* [Hidden] */
eps = 0.01;
$fn = 48;

/* ---- derived -------------------------------------------------------- */
pocket_w = board_w + 2 * fit;
pocket_h = board_h + 2 * (fit + fit_long);
z0 = bottom_h + bottom_clear; // PCB underside height
Hc = z0 + board_t + comp_h + comp_clear; // lid underside height
groove_z0 = Hc - gap;
tongue_o = gd - gap + 0.2; // tongue sticks out this far past the lid block
groove_zin = Hc + tongue_tip + (gd - gap) + 1.5 * gap; // groove ceiling at the wall's inner face
groove_zout = groove_zin - gd; // ... and at full groove depth (45 degree slope)
H = lid_mode == "slide" ? groove_zin + rail_t : Hc;
clip_base = z0 + board_t + t_play; // underside of the hooks
pad_h = Hc - (z0 + board_t) - pad_relief;
fill_top = lid_mode == "slide" ? Hc - gap : Hc;

// button tab geometry (board coordinates, z above the PCB underside)
btn_free_z = btn_z + 2.07; // free end; press-pad plateau centred on the button
btn_hinge_z = btn_free_z - btn_tab_len; // where the U-slot arms end
btn_uw = btn_tab_w + 2 * btn_slot; // overall U-slot width

// inner pocket extents
XL = -pocket_w / 2;
XR = sd_bay ? XL + sd_gap + sd_len + sd_pin + sd_gap : pocket_w / 2;
YH = pocket_h / 2;
YD = sd_bay ? -pocket_h / 2 - div : -pocket_h / 2; // top of the SD bay
YL = sd_bay ? YD - (sd_w + 2 * sd_gap) : -pocket_h / 2; // bottom inner face
W = XR - XL;
L = YH - YL;
xc = (XL + XR) / 2;
yc = (YL + YH) / 2;
ycb = (YD + YL) / 2; // SD bay centre
sd_x0 = XL + sd_gap; // module left edge
sd_zt = bottom_h + sd_stand + sd_pcb_t; // module PCB top

ears = [
  for (cx = [XL - ear_off, XR + ear_off], cy = [YL - ear_off, YH + ear_off]) [cx, cy],
];

// Clip placements: [z-rotation, inner-face distance, offset along wall, width,
//                   hook base z, window depth, hook reach, hook height].
// Board clips: short walls only (the long edges are lined with components).
// +Y end: the small parts at x -12..-4 reach y=39.5, so the left clip is moved
// and narrowed to x -19..-13. -Y end (over the SD-bay divider): nothing in the way.
clips = [
  [0, pocket_h / 2, -16.0, 6.0, clip_base, wall, hook_reach, hook_h],
  [0, pocket_h / 2, 16.0, 6.0, clip_base, wall, hook_reach, hook_h],
  [180, pocket_h / 2, -13.5, clip_w, clip_base, sd_bay ? div : wall, hook_reach, hook_h],
  [180, pocket_h / 2, 13.5, clip_w, clip_base, sd_bay ? div : wall, hook_reach, hook_h],
];

// SD module clips: the two left corners (beside the card slot, where the mounting
// holes are) and two on the far long edge. Positions are estimated from the photo -
// check them against your module and move them if a part is in the way.
sd_clip_base = sd_zt + t_play;
sd_edge = sd_w / 2 - 2.5;
sd_clips =
  (sd_bay && use_sd_clips) ? [
      [90, -XL, ycb + sd_edge, 3.5, sd_clip_base, wall, sd_hook_reach, sd_hook_h],
      [90, -XL, ycb - sd_edge, 3.5, sd_clip_base, wall, sd_hook_reach, sd_hook_h],
      [180, -YL, -(sd_x0 + 7.0), 5.0, sd_clip_base, wall, sd_hook_reach, sd_hook_h],
      [180, -YL, -(sd_x0 + 24.0), 5.0, sd_clip_base, wall, sd_hook_reach, sd_hook_h],
    ]
  : [];

module place(c) rotate([0, 0, c[0]]) translate([c[2], 0, 0]) children();

// recess through the wall so the finger can flex outward
module clip_window(inner, w, base, depth, hh) {
  translate([-(w + 2 * slot_w) / 2, inner - eps, bottom_h])
    cube([w + 2 * slot_w, depth + 2 * eps, base + 1 + hh - bottom_h + eps]);
}

// cantilever finger + retaining hook (catching face below, lead-in slope above)
module clip_finger(inner, w, base, reach, hh) {
  translate([-w / 2, inner - eps, bottom_h])
    cube([w, clip_t + eps, base - bottom_h]);
  hull()for (
    q = [
      [inner + clip_t, base],
      [inner + clip_t, base + hh],
      [inner - reach, base],
    ]
  )
    translate([0, q[0], q[1]]) cube([w, eps, eps], center=true);
}

module clip_cut(list) { for (c = list) place(c) clip_window(c[1], c[3], c[4], c[5], c[7]); }
module clip_add(list) { for (c = list) place(c) clip_finger(c[1], c[3], c[4], c[6], c[7]); }

// extrude a 2D profile drawn in (X, Z) along Y from y0 to y1
module extrude_y(y0, y1) {
  translate([0, y1, 0]) rotate([90, 0, 0]) linear_extrude(y1 - y0) children();
}

// groove cross-section (X, Z) for one long wall; ceiling slopes 45 degrees
module groove_profile(side) {
  x_in = side > 0 ? XR - eps : XL + eps;
  x_out = side > 0 ? XR + gd : XL - gd;
  polygon(
    [
      [x_in, groove_z0],
      [x_out, groove_z0],
      [x_out, groove_zout],
      [x_in, groove_zin],
    ]
  );
}

module rrect(w, h, r) {
  if (r <= 0)
    square([w, h], center=true);
  else
    offset(r=r) offset(r=-r) square([w, h], center=true);
}

module outline2d() {
  translate([xc, yc]) rrect(W + 2 * wall, L + 2 * wall, corner_r);
  if (lid_mode == "screw") for (e = ears) translate(e) circle(r=ear_r);
}

// ---- left-wall openings (shapes copied from display_outer_enclosure.stl) ----
// rounded-rectangle hole through the left wall; yc / zc = centre (z above PCB underside)
module left_wall_cut(yc, zc, w, h, r) {
  translate([XL - wall - 0.5, yc, z0 + zc])
    rotate([90, 0, 90]) linear_extrude(wall + 1.0)
        rrect(w, h, min(r, h / 2 - 0.01, w / 2 - 0.01));
}

module left_rect_cut(yc, w, zlo, zhi) {
  translate([XL - wall - 0.5, yc - w / 2, z0 + zlo]) cube([wall + 1.0, w, zhi - zlo]);
}

// U-shaped slot profile in (Y, Z): arms end at the hinge, closed end is rounded
module btn_slot2d() {
  ro = 1.75;
  rt = max(ro - btn_slot, 0.3);
  difference() {
    hull() {
      translate([-btn_uw / 2, btn_hinge_z]) square([btn_uw, 0.01]);
      for (sx = [-1, 1])
        translate([sx * (btn_uw / 2 - ro), btn_free_z + btn_slot - ro]) circle(r=ro);
    }
    hull() {
      translate([-btn_tab_w / 2, btn_hinge_z - 1]) square([btn_tab_w, 0.01]);
      for (sx = [-1, 1])
        translate([sx * (btn_tab_w / 2 - rt), btn_free_z - rt]) circle(r=rt);
    }
  }
}

module button_cover_cut(yb) {
  // U-slot through the wall, leaving a cantilever tab hinged on the PCB side
  translate([XL - wall - 0.5, yb, z0]) rotate([90, 0, 90])
      linear_extrude(wall + 1.0) btn_slot2d();
  // tab inner face recessed so the button tip clears it
  translate([XL - btn_recess, yb - btn_tab_w / 2, z0 + btn_hinge_z])
    cube([btn_recess + 0.5, btn_tab_w, btn_tab_len]);
  // flexure relief at the hinge
  if (btn_relief)
    translate([XL - btn_relief_depth, yb - btn_tab_w / 2, z0 + btn_hinge_z])
      cube([btn_relief_depth + 0.5, btn_tab_w, btn_relief_len]);
}

// raised press pad on the tab: 45 degree chamfer toward the free end, gentle ramp toward the hinge
module button_pad(yb) {
  xo = XL - wall;
  zf = z0 + btn_free_z;
  ramp = 1.79 * btn_pad_h;
  intersection() {
    extrude_y(yb - btn_tab_w / 2, yb + btn_tab_w / 2)
      polygon(
        [
          [xo + eps, zf],
          [xo - 0.21, zf],
          [xo - btn_pad_h, zf - 0.78],
          [xo - btn_pad_h, zf - 3.36],
          [xo + eps, zf - 3.36 - ramp],
        ]
      );
    translate([0, 0, zf - 3.36 - ramp]) linear_extrude(3.36 + ramp)
        polygon(
          [
            [xo + eps, yb - btn_tab_w / 2],
            [xo + eps, yb + btn_tab_w / 2],
            [xo - btn_pad_h, yb + btn_tab_w / 2 - 0.8 * btn_pad_h],
            [xo - btn_pad_h, yb - btn_tab_w / 2 + 0.8 * btn_pad_h],
          ]
        );
  }
}

// keep the PCB standoff ring out of the way of the tab hinge
module btn_ring_notch(yb) {
  translate([XL - 1, yb - btn_tab_w / 2 - 0.3, bottom_h - 1])
    cube([1 + 1.6, btn_tab_w + 0.6, bottom_clear + 1.1]);
}

module right_cut(y, w) {
  translate([pocket_w / 2 - 0.5, y - w / 2, z0 + 1.4])
    cube([XR - pocket_w / 2 + wall + 1.0, w, 3.4]);
}

// pocket volume; the strip beside the board stays solid so the board sits against the left wall
module pocket3d() {
  difference() {
    translate([xc, yc, bottom_h]) linear_extrude(H) rrect(W, L, pocket_r);
    if (sd_bay)
      translate([pocket_w / 2, -pocket_h / 2, bottom_h - 1])
        cube([XR - pocket_w / 2 + 1, pocket_h + 1, fill_top - bottom_h + 1]);
  }
}

module frame() {
  difference() {
    linear_extrude(H) outline2d();
    pocket3d();
    // left wall: USB-C, power switch, buttons
    left_wall_cut(usb_y, usb_z, usb_w, usb_h, usb_r);
    left_wall_cut(sw_y, sw_z, sw_w, sw_h, sw_r);
    for (yb = btn_y)
      if (btn_covers)
        button_cover_cut(yb);
      else
        left_rect_cut(yb, 4.6, 2.2, 5.5);
    if (use_clips) clip_cut(clips);
    clip_cut(sd_clips);
    // left wall: microSD card slot
    if (sd_bay)
      translate([XL - wall - 0.5, ycb - sd_slot_w / 2, sd_zt - 0.3])
        cube([wall + 1.0, sd_slot_w, 3.2]);
    // right wall: 50-pin FPC ribbon slot
    right_cut(fpc_y, fpc_w);
    if (fpc2_cutout) right_cut(9.75, 19);
    if (battery_cutout)
      translate([battery_x - battery_w / 2, YH - 0.5, z0 + 1.5])
        cube([battery_w, wall + 1.0, 5.0]);
    // wall between board pocket and SD bay lowered to PCB-back height
    if (sd_bay)
      translate([XL, YD - eps, z0 + board_t])
        cube([W, div + 2 * eps, H]);
    if (lid_mode == "screw")
      for (e = ears)
        translate([e[0], e[1], H - screw_depth]) cylinder(d=screw_pilot, h=screw_depth + eps);
    if (lid_mode == "slide") {
      // grooves in both long walls, open at the bottom (-Y) end
      extrude_y(YL - wall - 1, YH) groove_profile(1);
      extrude_y(YL - wall - 1, YH) groove_profile(-1);
      // bottom end wall lowered to the PCB back face: the lid enters here
      translate([XL, YL - wall - 1, z0 + board_t])
        cube([W, wall + 1 + eps, H + 1]);
    }
  }
  // PCB standoff ring (board area only)
  difference() {
    translate([0, 0, bottom_h - eps])
      linear_extrude(bottom_clear + eps)
        difference() {
          rrect(pocket_w, pocket_h, pocket_r);
          rrect(board_w - 2 * ledge_w, board_h - 2 * ledge_w, 2.0);
        }
    if (btn_covers) for (yb = btn_y) btn_ring_notch(yb);
  }
  // raised press pads on the button tabs
  if (btn_covers) for (yb = btn_y) button_pad(yb);
  // snap-clip fingers (added after the cuts)
  if (use_clips) clip_add(clips);
  clip_add(sd_clips);
  // SD module supports
  if (sd_bay) sd_supports();
  // detent bumps in the grooves
  if (lid_mode == "slide" && bump_h > 0)
    for (bx = [XL - gd / 2, XR + gd / 2])
      intersection() {
        translate([bx, YL + 4, groove_z0 + bump_h - bump_r]) sphere(r=bump_r);
        translate([bx - gd / 2, YL, groove_z0 - eps]) cube([gd, L, bump_h + eps]);
      }
}

module sd_supports() {
  for (sy = [-1, 1]) {
    // left corners (optionally with locating pins for the mounting holes)
    translate([sd_x0 + 2.5, ycb + sy * (sd_w / 2 - 2.5), bottom_h - eps]) {
      cylinder(d=4.0, h=sd_stand + eps);
      if (sd_pins) cylinder(d=2.0, h=sd_stand + sd_pcb_t + 0.6);
    }
    // right corners
    translate([sd_x0 + sd_len - 3.0, ycb + sy * (sd_w / 2 - 2.5), bottom_h - eps])
      cylinder(d=3.0, h=sd_stand + eps);
  }
}

module sd_ghost() {
  translate([sd_x0, ycb - sd_w / 2, bottom_h + sd_stand]) {
    cube([sd_len, sd_w, sd_pcb_t]);
    translate([0.5, 4.5, sd_pcb_t]) cube([16, 15, 2]); // socket
    translate([sd_len - 4, 4.5, sd_pcb_t]) cube([2.5, 15, 2.5]); // header housing
    translate([sd_len - 3, 4.5, sd_pcb_t + 1]) cube([sd_pin + 3, 15, 0.6]); // pins
  }
}

module pads() {
  for (sx = [-1, 1], sy = [-1, 1])
    translate([sx * pad_x - pad / 2, sy * pad_y - pad / 2, Hc - pad_h])
      cube([pad, pad, pad_h + eps]);
}

module lid_slide() {
  bw = W - 0.4; // centre block, fits between the walls
  tw = W + 2 * (gd - gap); // tongue width, rides in the grooves
  difference() {
    union() {
      // centre block, flush with the frame top; reaches the bottom outer face
      translate([xc - bw / 2, YL - wall + 0.15, Hc])
        cube([bw, YH - 0.1 - (YL - wall + 0.15), H - Hc]);
      // tongue edges: top face slopes 45 degrees so the lid prints without supports
      // (printed outer face down, this slope is the underside of the overhang)
      extrude_y(YL, YH - 0.1)
        polygon(
          [
            [xc - tw / 2, Hc],
            [xc + tw / 2, Hc],
            [xc + tw / 2, Hc + tongue_tip],
            [xc + bw / 2, Hc + tongue_tip + tongue_o],
            [xc - bw / 2, Hc + tongue_tip + tongue_o],
            [xc - tw / 2, Hc + tongue_tip],
          ]
        );
      // end cap that fills the lowered bottom wall
      translate([xc - bw / 2, YL - wall + 0.15, z0 + board_t + 0.15])
        cube([bw, wall - 0.15, Hc - (z0 + board_t + 0.15) + eps]);
      if (slide_pads) pads();
    }
    // dimples that seat on the detent bumps
    if (bump_h > 0)
      for (bx = [XL - gd / 2, XR + gd / 2])
        translate([bx, YL + 4, groove_z0 + bump_h - bump_r]) sphere(r=bump_r + 0.1);
  }
}

module lid_screw() {
  difference() {
    union() {
      translate([0, 0, H]) linear_extrude(lid_t) outline2d();
      pads();
    }
    for (e = ears) {
      translate([e[0], e[1], H - eps]) cylinder(d=screw_clear, h=lid_t + 2 * eps);
      translate([e[0], e[1], H + lid_t - 1.0])
        cylinder(d1=screw_clear, d2=screw_head, h=1.0 + eps);
    }
  }
}

module lid() {
  if (lid_mode == "slide") lid_slide(); else lid_screw();
}

lid_top = lid_mode == "slide" ? H : H + lid_t;
module lid_print() { rotate([180, 0, 0]) translate([0, 0, -lid_top]) lid(); }

if (part == "frame")
  frame();
else if (part == "lid")
  lid_print();
else if (part == "both") {
  frame();
  translate([W + 2 * wall + 12, 0, 0]) lid_print();
} else {
  color("DimGray") frame();
  color("SteelBlue") lid();
  if (show_board) color("green", 0.6) translate([0, 0, z0]) import("board.stl");
  if (show_sd && sd_bay) color("orange", 0.7) sd_ghost();
}
