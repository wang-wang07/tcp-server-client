#!/usr/bin/env perl
use strict;
use warnings;
use IO::Socket::INET;
use IO::Select;
use Socket qw(SOL_SOCKET SO_RCVBUF SO_LINGER);
use Time::HiRes qw(time sleep);
use File::Temp qw(tempdir);
use POSIX qw(WNOHANG sysconf _SC_CLK_TCK);

# Usage: perl this-file.pl /absolute/path/to/server
# Run with port 8080 free. The harness owns its child process and its sockets;
# it never kills an unrelated server. Temporary logs remain available on failure.
my $binary = shift @ARGV or die "pass the server executable path\n";
my $dir = tempdir('epoll-server-XXXXXX', TMPDIR => 1, CLEANUP => 0);
my $pid = fork();
defined($pid) or die "fork: $!"; # undef means no child was created.
if ($pid == 0) {
    # fork returns zero only in the child. Replace that child with the server,
    # redirecting both output streams so diagnostics cannot fill a pipe.
    open STDOUT, '>', "$dir/server.log" or die $!;
    open STDERR, '>&', \*STDOUT or die $!;
    exec $binary;
    die "exec: $!"; # Successful exec never returns.
}
END {
    my $status = $?; # Child cleanup must not replace the harness exit status.
    if ($pid) {
        # WNOHANG returns zero only while our child is still running. Startup
        # checks may have reaped a failed child; never signal its reusable pid.
        my $state = waitpid($pid, WNOHANG);
        if ($state == 0) {
            kill 'TERM', $pid;
            waitpid($pid, 0);
        }
    }
    $? = $status;
}
$SIG{ALRM} = sub { die "overall 90-second test deadline exceeded; logs=$dir\n"; };
alarm 90; # Also bounds a blocking connect or write if progress breaks.

sub connect_client {
    # Each returned object owns a separate TCP socket; dropping/closing it
    # releases the client's descriptor, independently of the server's owner.
    return IO::Socket::INET->new(PeerAddr => '127.0.0.1', PeerPort => 8080,
                                 Proto => 'tcp', Timeout => 2);
}
my $probe;
for (1..100) {
    # A terminated child usually means bind failed; do not test another server.
    die "server exited during startup; logs=$dir\n"
        if waitpid($pid, WNOHANG) == $pid;
    $probe = connect_client();
    last if $probe; # A socket means the listener is accepting connections.
    sleep 0.02;    # Allow initialization without a busy retry loop.
}
die "server did not start\n" unless $probe;
sleep 0.05;
die "server failed during startup; logs=$dir\n"
    if waitpid($pid, WNOHANG) == $pid;

sub send_bytes {
    my ($socket, $bytes) = @_;
    my $offset = 0; # Prefix already accepted by the client's kernel send queue.
    while ($offset < length($bytes)) {
        # Request only the unsent suffix; syswrite may accept a shorter prefix.
        my $n = syswrite($socket, $bytes, length($bytes) - $offset, $offset);
        die "client write: $!\n" unless defined($n) && $n > 0;
        $offset += $n; # Retain every byte not accepted by this call.
    }
}
sub expect_bytes {
    my ($socket, $expected) = @_;
    my $actual = '';
    while (length($actual) < length($expected)) {
        # Readiness has a finite deadline so a stranded buffered command fails.
        die "reply stalled\n" unless IO::Select->new($socket)->can_read(2);
        my $n = sysread($socket, my $chunk, length($expected) - length($actual));
        die "early EOF/read failure\n" unless defined($n) && $n > 0;
        $actual .= $chunk; # TCP chunks are not response boundaries.
    }
    die "reply mismatch\n" unless $actual eq $expected;
}
sub expect_closed {
    my ($socket) = @_;
    # Rejected clients may see FIN or reset. In both cases they must stop being
    # serviceable; a timeout or a reply byte means the expected close failed.
    die "connection did not close\n" unless IO::Select->new($socket)->can_read(2);
    my $n = sysread($socket, my $byte, 1);
    die "unexpected data/error instead of close\n"
        unless (defined($n) && $n == 0) || (!defined($n) && $!{ECONNRESET});
}
sub metrics {
    # Linux /proc snapshots expose resident memory and cumulative process CPU.
    # Removing the parenthesized comm field makes subsequent stat indices
    # independent of spaces in the executable name.
    open my $stat, '<', "/proc/$pid/stat" or die $!;
    my $line = <$stat>;
    $line =~ s/^\d+ \(.*\) //;
    my @fields = split /\s+/, $line;
    my $ticks = $fields[11] + $fields[12]; # Original fields 14 and 15.
    open my $status, '<', "/proc/$pid/status" or die $!;
    my $rss = 0;
    while (my $entry = <$status>) {
        if ($entry =~ /^VmRSS:\s+(\d+)/) {
            # VmRSS reports resident pages in KiB; this is a sample, not a
            # precise count of allocated C++ buffers or a continuous peak.
            $rss = $1;
        }
    }
    return ($ticks, $rss);
}

# Confirm 128 sockets were actually registered by receiving a reply on each.
# Merely completing connect() would prove only admission to the TCP backlog.
my @clients = ($probe);
for my $i (0..127) {
    if ($i != 0) {
        # Slot zero already owns the startup probe; create the other 127.
        # Check the socket result itself; push returns array length even when
        # passed undef and therefore cannot detect a failed connection.
        my $next = connect_client() or die "connect $i: $!";
        push @clients, $next;
    }
    send_bytes($clients[$i], "EXISTS __p11_missing\n");
    expect_bytes($clients[$i], "0\n");
}
my $overflow = connect_client() or die $!;
expect_closed($overflow);
close $overflow;
print "PASS 128 active clients; client 129 rejected\n";

# No byte progress occurs on these clients. Wait past 30 seconds plus the
# one-second timer polling interval, then require every server socket to close.
sleep 31.5;
for my $socket (@clients) {
    expect_closed($socket);
    close $socket;
}
@clients = ();
print "PASS idle clients expire and capacity becomes reusable\n";

my $fast = connect_client() or die $!;
# More than 32 complete frames must finish even without another client send
# or EOF notification. Exact replies detect both missing and replayed frames.
send_bytes($fast, "EXISTS __p11_missing\n" x 1000);
expect_bytes($fast, "0\n" x 1000);
print "PASS buffered commands continue without another send\n";

my $oversized = connect_client() or die $!;
# A frame needs its newline inside 65536 bytes; a full buffer without one
# cannot become valid, so retirement must occur immediately, not at idle expiry.
send_bytes($oversized, 'x' x 65536);
expect_closed($oversized);
close $oversized;
print "PASS full unterminated input is rejected\n";

my $value = 'v' x 8192;
send_bytes($fast, "SET __p11_large $value\n");
expect_bytes($fast, "OK\n");
my $slow = connect_client() or die $!;
setsockopt($slow, SOL_SOCKET, SO_RCVBUF, pack('i', 4096)) or die $!;
# These requests ask for over 16 MiB of replies. The peer withholds reads;
# the server must eventually pause input rather than grow its output forever.
# Socketpair unit tests give deterministic would-block coverage independent
# of this machine's TCP autotuning and receive-window behavior.
send_bytes($slow, "GET __p11_large\n" x 2000);
sleep 0.1;
my ($ticks_before, $peak_rss) = metrics();
my $started = time();
my @latency;
for (1..2000) {
    my $request_start = time();
    send_bytes($fast, "EXISTS __p11_missing\n");
    expect_bytes($fast, "0\n");
    push @latency, (time() - $request_start) * 1000;
    my (undef, $rss) = metrics();
    # Retain the largest sampled resident-memory value during this workload.
    $peak_rss = $rss if $rss > $peak_rss;
}
my $elapsed = time() - $started;
my ($ticks_after) = metrics();
my @sorted = sort { $a <=> $b } @latency;
my $cpu = 100 * ($ticks_after - $ticks_before) / sysconf(_SC_CLK_TCK) / $elapsed;
printf "PASS concurrent reader: %.0f requests/s, p95 %.3f ms, CPU %.1f%%, sampled RSS peak %d KiB\n",
       2000 / $elapsed, $sorted[int(0.95 * $#sorted)], $cpu, $peak_rss;

# Deliberately reset the slow peer rather than reading 16 MiB in this check.
# Pending replies may be discarded on reset; the responsive client must survive.
setsockopt($slow, SOL_SOCKET, SO_LINGER, pack('ii', 1, 0)) or die $!;
close $slow;
send_bytes($fast, "DELETE __p11_large\n");
expect_bytes($fast, "OK\n");
# Exercise EOF only after many budgeted commands; all responses precede FIN.
send_bytes($fast, "EXISTS __p11_missing\n" x 20000);
shutdown($fast, 1) or die $!;
expect_bytes($fast, "0\n" x 20000);
expect_closed($fast);
close $fast;
# Sanitizer failures can appear in the server log even when reply checks passed.
# Treat those diagnostics as failure; terminating the child is not a clean-exit
# LeakSanitizer audit, which remains outside this harness.
open my $log, "<", "$dir/server.log" or die $!;
my $log_text = do { local $/; <$log> };
die "sanitizer diagnostic; logs=$dir\n"
    if $log_text =~ /AddressSanitizer|LeakSanitizer|runtime error:/;
print "PASS reset isolation and budgeted half-close draining; logs=$dir\n";
alarm 0;
