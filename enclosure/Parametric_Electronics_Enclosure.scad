//////////////////////////////////////////////////////////////
// PARAMETRIC ENCLOSURE WITH OPTIONAL ROUNDED CORNERS
// Includes:
// - Top enclosure (box without bottom plate)
// - Bottom plate with screw holes
// - Automatically centered PCB standoffs
// - Optional heat‑set inserts (brass)
//
// This design is fully parametric:
// simply adjust the values below and the model updates accordingly.
//
// Final version (Dany Van Impe) – improved PCB standoffs
//////////////////////////////////////////////////////////////


//////////////////////////////
// 📐 BOX PARAMETERS
//////////////////////////////

// Outer dimensions of the enclosure.
// Choose these values depending on your electronics.
box_length = 90;        // Length of the box (left ↔ right)
box_width  = 65;        // Width of the box (front ↔ back)
box_height = 35;        // Height of the box (bottom ↕ top)

// Wall thickness of the enclosure.
// More thickness = stronger, but longer print time.
wall = 1.5;

// Thickness of the bottom plate.
// Typically between 1.5 and 2.5 mm.
plate_thickness = 2;

// Extra clearance so components do not fit too tightly.
// 0.25 mm is a safe default.
oversize = 0.25;

// Corner rounding radius.
// 0 = sharp corners, 2–4 mm = nicely rounded.
corner_radius = 3;

// Enable PCB standoffs on the bottom plate.
// true  = standoffs visible
// false = no standoffs
use_mounts = true;

// What do you want to render?
// 0 = show everything
// 1 = only the box
// 2 = only the bottom plate
print = 0;


//////////////////////////////
// 🔩 INSERT PARAMETERS
//////////////////////////////

// Choose between standard screw holes or heat‑set inserts.
// false = classic screw holes (M3 screws)
// true  = brass heat‑set inserts melted into the pillars
use_inserts = true;

// Insert type used.
// Diameter and depth are automatically retrieved.
insert_type = "M3x5";   // common for small electronics

// Function returning the correct insert diameter (mm)
// based on the chosen insert type.
// Works like a small lookup table:
//
//   M2x3   → 2.9 mm
//   M2x4   → 2.9 mm
//   M2.5x4 → 3.4 mm
//   M3x4   → 4.1 mm
//   M3x5   → 4.1 mm
//
// Unknown types return 'undef'.
function get_insert_diameter(t) =
    t == "M2x3"   ? 2.9 :
    t == "M2x4"   ? 2.9 :
    t == "M2.5x4" ? 3.4 :
    t == "M3x4"   ? 4.1 :
    t == "M3x5"   ? 4.1 :
    undef;

function get_insert_depth(t) =
    t == "M2x3"   ? 3.2 :
    t == "M2x4"   ? 4.2 :
    t == "M2.5x4" ? 4.2 :
    t == "M3x4"   ? 4.2 :
    t == "M3x5"   ? 5.2 :
    undef;


//////////////////////////////
// 🪛 COUNTERSINK PARAMETERS
//////////////////////////////

// Enable countersunk screw heads.
// true  = screw head sits flush in the bottom plate
// false = standard cylindrical screw head
countersink = true;

// Diameter of the screw head (top side).
// For M3 screws, 4 mm is typical.
cs_head_diameter = 4.0;

// Depth of the countersink.
// 1.5 mm is usually sufficient.
cs_head_height   = 1.5;




//////////////////////////////
// 🧩 PCB PARAMETERS
//////////////////////////////

// Distance between the mounting holes of your PCB.
// Simply measure the hole spacing.
// Standoffs are automatically centered on the bottom plate.
pcb_hole_distance_x = 65;    // horizontal PCB hole spacing
pcb_hole_distance_y = 45;    // vertical PCB hole spacing

// Height of the PCB above the bottom plate.
// 6–10 mm is typical.
pcb_mount_height    = 8;

// Screw hole radius inside the PCB standoff.
// 0.5 mm works for M2/M2.5 screws.
pcb_screw_radius    = 0.5;

// Optional PCB offset.
// [0,0] = perfectly centered.
// Useful if you need a connector aligned to one side.
pcb_center_offset = [0,0];

// Automatic calculation of the four standoff positions.
pcb_mount_positions = [
    [-pcb_hole_distance_x/2 + pcb_center_offset[0],
     -pcb_hole_distance_y/2 + pcb_center_offset[1]],

    [ pcb_hole_distance_x/2 + pcb_center_offset[0],
     -pcb_hole_distance_y/2 + pcb_center_offset[1]],

    [ pcb_hole_distance_x/2 + pcb_center_offset[0],
      pcb_hole_distance_y/2 + pcb_center_offset[1]],

    [-pcb_hole_distance_x/2 + pcb_center_offset[0],
      pcb_hole_distance_y/2 + pcb_center_offset[1]]
];


//////////////////////////////
// 📏 DERIVED DIMENSIONS
//////////////////////////////

// Internal dimensions of the enclosure.
// Automatically calculated based on wall thickness + oversize.
inside_length = box_length - 2*wall + oversize;
inside_width  = box_width  - 2*wall + oversize;
inside_height = box_height - plate_thickness;

// Dimensions of the corner pillars.
pillar_radius = 3;
pillar_height = inside_height - plate_thickness - 1;

// Automatic calculation of pillar positions.
mount_offset_x = inside_length/2 - pillar_radius;
mount_offset_y = inside_width/2  - pillar_radius;

// Four corner positions (front‑left, front‑right, rear‑right, rear‑left)
mount_positions = [
    [-mount_offset_x, -mount_offset_y],
    [ mount_offset_x, -mount_offset_y],
    [ mount_offset_x,  mount_offset_y],
    [-mount_offset_x,  mount_offset_y]
];


//////////////////////////////
// 🟦 MODULE: rounded box
//////////////////////////////

// Creates a box with optional rounded corners.
// r=0 → standard rectangular shape
// r>0 → Minkowski smoothing for rounded edges
module rounded_box(size=[10,10,10], r=0) {
    if (r == 0) {
        cube(size, center=true);
    } else {
        minkowski() {
            cube(size - [2*r, 2*r, 2*r], center=true);
            sphere(r=r, $fn=40);
        }
    }
}


//////////////////////////////
// 🟦 MODULE: box shell (no bottom)
//////////////////////////////

module box_shell() {

    difference() {

        // Outer shape of the enclosure
        translate([0,0, box_height/2])
            rounded_box([box_length, box_width, box_height], r=corner_radius);

        // Hollow interior
        translate([0,0, plate_thickness + inside_height/2])
            cube([inside_length, inside_width, inside_height], center=true);
    }

    // Corner pillars
    for (p = mount_positions)
        mount_pillar(p[0], p[1]);
}


//////////////////////////////
// 🟦 MODULE: screw pillar
//////////////////////////////

// Vertical pillars used to fasten the bottom plate.
// Choose between standard screw holes or heat‑set inserts.
module mount_pillar(x, y) {

    pillar_start_z = plate_thickness;

    d = get_insert_diameter(insert_type);
    h = get_insert_depth(insert_type);

    translate([x, y, pillar_start_z + pillar_height/2])
    difference() {

        // Outer pillar
        cylinder(r=pillar_radius, h=pillar_height, center=true, $fn=40);

        // Inner hole
        if (use_inserts) {
            // Hole for heat‑set insert
            translate([0,0,(pillar_height/2 - h)])
                cylinder(r=d/2, h=h+0.2, center=false, $fn=40);
        } else {
            // Standard screw hole
            cylinder(r=1.25, h=pillar_height+0.2, center=true, $fn=40);
        }
    }
}


//////////////////////////////
// 🟦 MODULE: PCB standoffs
//////////////////////////////

// Four standoffs matching the PCB hole pattern.
// Ensures the PCB sits level.
module pcb_mounts() {
    for (p = pcb_mount_positions)
        translate([p[0], p[1], 0])
            difference() {
                cylinder(r=2.5, h=pcb_mount_height, center=false, $fn=40);
                cylinder(r=pcb_screw_radius, h=pcb_mount_height+0.2, center=false, $fn=40);
            }
}


//////////////////////////////
// 🟦 MODULE: bottom plate
//////////////////////////////

module bottom_plate() {

    // Bottom plate is drawn next to the box (useful for printing)
    translate([0, -(box_width + 10), 0]) {

        difference() {

            // The plate itself
            translate([0,0, plate_thickness/2])
                cube([inside_length, inside_width, plate_thickness], center=true);

            // Screw holes + countersink
            for (p = mount_positions) {

                // Through‑hole
                translate([p[0], p[1], 0])
                    cylinder(r=1.6, h=plate_thickness+0.2, center=false, $fn=40);

                // Countersunk head (OUTSIDE of the plate)
                if (countersink) {
                    translate([p[0], p[1], 0])   // start at bottom side
                        cylinder(
                            h = cs_head_height,
                            r1 = cs_head_diameter/2,
                            r2 = 1.6,
                            center = false,
                            $fn = 40
                        );
                }
            }
        }

        // Add PCB standoffs if enabled
        if (use_mounts)
            pcb_mounts();
    }
}


//////////////////////////////
// 🟦 FINAL STRUCTURE
//////////////////////////////

if (print == 0 || print == 1)
    box_shell();

if (print == 0 || print == 2)
    bottom_plate();
