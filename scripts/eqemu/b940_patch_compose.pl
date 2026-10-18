#!/usr/bin/perl
# B940 (#258): add PROCESS_WATCHER_EXCLUDE to the eqemu-server service's
# compose environment (one line; the watcher reads the var and the value is
# the exclusion regex, see b940_patch_watcher.pl).
#
# Idempotent: refuses when any PROCESS_WATCHER_EXCLUDE is already present;
# refuses when the eqemu-server service or its environment block is missing
# (a stack whose compose has drifted is an operator problem, not a blind
# edit).
#
# usage: b940_patch_compose.pl <docker-compose.yml> <exclude-value>
use strict;
use warnings FATAL => 'all';

my ($path, $value) = @ARGV;
die "usage: b940_patch_compose.pl <docker-compose.yml> <exclude-value>\n"
    unless defined $path && defined $value && $value ne '';
$value =~ s/^\s+|\s+$//g;

open my $fh, '<', $path or die "cannot read $path: $!\n";
my $src = do { local $/; <$fh> };
close $fh;

if ($src =~ /PROCESS_WATCHER_EXCLUDE/) {
    print "compose: already provisioned (PROCESS_WATCHER_EXCLUDE present)\n";
    exit 0;
}

my @lines = split /\n/, $src, -1;
my ($in_svc, $env_idx, $svc_found, $svc_indent) = (0, -1, 0, 2);
for my $i (0 .. $#lines) {
    my $l = $lines[$i];
    if ($l =~ /^(\s*)eqemu-server\s*:/) {
        $svc_found  = 1;
        $svc_indent = length($1);
        $in_svc     = 1;
        next;
    }
    next unless $in_svc;
    if ($l =~ /^(\s*)\S/ && length($1) <= $svc_indent) {
        # A new top-level (or sibling service) line ends the service block.
        $in_svc = 0;
    }
    if ($in_svc && $l =~ /^\s*environment\s*:/ && $env_idx < 0) {
        $env_idx = $i;
    }
}
die "compose: no 'eqemu-server' service found -- refused\n" unless $svc_found;
die "compose: eqemu-server has no 'environment:' block -- refused\n" if $env_idx < 0;

# The env list's items are indented deeper than the 'environment:' key;
# match the first item's indentation for the new line (the compose here
# uses six spaces).
my $item_indent = '      ';
for my $j ($env_idx + 1 .. $#lines) {
    if ($lines[$j] =~ /^(\s*)-\s/) {
        $item_indent = $1;
        last;
    }
    last if $lines[$j] =~ /^\S/ || $lines[$j] =~ /^(\s*)[\w-]+\s*:/;
}
my $new_line = "${item_indent}- PROCESS_WATCHER_EXCLUDE=${value}  # B940: exempt the zone binaries";
splice @lines, $env_idx + 1, 0, $new_line;

open my $out, '>', $path or die "cannot write $path: $!\n";
print $out join("\n", @lines);
close $out;
print "compose: PROCESS_WATCHER_EXCLUDE added to the eqemu-server service\n";
