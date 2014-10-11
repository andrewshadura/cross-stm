#!/usr/bin/env tclsh

gets stdin fmt
if {$fmt ne "P4"} {
    error "Not P4 pnm"
}

gets stdin geom
lassign $geom width height

set bwidth [expr {$width / 8}]

set bitmap {}

set i 0

while {$i < $height} {
    set line [read stdin $bwidth]
    dict lappend bitmap $line $i
    incr i
}

set name [lindex $argv 0]
if {$name eq ""} {
    set name bitmap
}

puts "#define ${name}_width $width"
puts "#define ${name}_height $height"
puts "const char ${name}_lines\[[llength [dict keys $bitmap]]]\[${name}_width / 8] = \{"
dict for {k v} $bitmap {
    binary scan $k b* r
    set v [binary format B* $r]
    puts "    {[string trim [regsub -all .. [binary encode hex $v] {0x&, }] ", "]},"
}
puts "};"

if {$height < 255} {
    puts "const unsigned char ${name}_bits\[${name}_height] = \{"
} else {
    puts "const unsigned short ${name}_bits\[${name}_height] = \{"
}

set i 0
dict for {k v} $bitmap {
    foreach l $v {
        puts "    \[$l] = $i,"
    }
    incr i
}

puts "};"
