use clap::{Args, Parser, Subcommand};
use std::ffi::CString;
use std::mem;
use std::os::unix::io::RawFd;
use std::time::{Duration, Instant};

const SOL_CAN_RAW: i32 = 101;
const CAN_RAW_FILTER: i32 = 1;
const CAN_RAW_LOOPBACK: i32 = 3;
const CAN_RAW_RECV_OWN_MSGS: i32 = 4;
const CAN_RAW_FD_FRAMES: i32 = 5;

const CAN_EFF_FLAG: u32 = 0x8000_0000;
const CAN_RTR_FLAG: u32 = 0x4000_0000;
const CAN_ERR_FLAG: u32 = 0x2000_0000;
const CAN_SFF_MASK: u32 = 0x0000_07FF;
const CAN_EFF_MASK: u32 = 0x1FFF_FFFF;

#[repr(C)]
#[derive(Clone, Copy)]
struct CanFrame {
    can_id: u32,
    can_dlc: u8,
    __pad: u8,
    __res0: u8,
    __res1: u8,
    data: [u8; 8],
}

#[repr(C)]
#[derive(Clone, Copy)]
struct CanFdFrame {
    can_id: u32,
    len: u8,
    flags: u8,
    __res0: u8,
    __res1: u8,
    data: [u8; 64],
}

#[repr(C)]
#[derive(Clone, Copy)]
struct CanFilter {
    can_id: u32,
    can_mask: u32,
}

#[derive(Parser)]
#[command(name = "can-standin", about = "High-performance SocketCAN sender/receiver for J2534 stand-in tests.")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    Sender(SenderOpts),
    Receiver(ReceiverOpts),
}

#[derive(Args)]
struct SenderOpts {
    #[arg(long, default_value = "can0")]
    iface: String,
    #[arg(long, value_parser = parse_u32, default_value = "0x123")]
    id: u32,
    #[arg(long)]
    extended: bool,
    #[arg(long)]
    fd: bool,
    #[arg(long)]
    quality_test: bool,
    #[arg(long, value_parser = parse_u8, default_value = "1")]
    test_id: u8,
    #[arg(long)]
    len: Option<usize>,
    #[arg(long)]
    data: Option<String>,
    #[arg(long, value_parser = parse_u8, default_value = "0xaa")]
    fill: u8,
    #[arg(long)]
    counter: bool,
    #[arg(long, value_parser = parse_u64, default_value = "0")]
    count: u64,
    #[arg(long = "loop", value_parser = parse_u64, default_value = "1")]
    loop_count: u64,
    #[arg(long, value_parser = parse_u64, default_value = "0")]
    rate: u64,
    #[arg(long, value_parser = parse_f64)]
    delay_ms: Option<f64>,
    #[arg(long, value_parser = parse_u64)]
    duration: Option<u64>,
    #[arg(long, value_parser = parse_u64, default_value = "1")]
    stats_interval: u64,
    #[arg(long)]
    quiet: bool,
    #[arg(long, default_value = "4194304")]
    tx_buf: usize,
    #[arg(long, default_value = "1048576")]
    rx_buf: usize,
    #[arg(long = "no-loopback")]
    no_loopback: bool,
    #[arg(long)]
    recv_own: bool,
}

#[derive(Args)]
struct ReceiverOpts {
    #[arg(long, default_value = "can0")]
    iface: String,
    #[arg(long, value_parser = parse_u32)]
    id: Option<u32>,
    #[arg(long, value_parser = parse_u32)]
    mask: Option<u32>,
    #[arg(long)]
    extended: bool,
    #[arg(long)]
    fd: bool,
    #[arg(long)]
    quality_test: bool,
    #[arg(long, value_parser = parse_u8)]
    test_id: Option<u8>,
    #[arg(long, value_parser = parse_u64, default_value = "0")]
    count: u64,
    #[arg(long, value_parser = parse_u64)]
    duration: Option<u64>,
    #[arg(long, value_parser = parse_u64, default_value = "1")]
    stats_interval: u64,
    #[arg(long, value_parser = parse_u64, default_value = "1000")]
    idle_exit_ms: u64,
    #[arg(long)]
    quiet: bool,
    #[arg(long)]
    dump: bool,
    #[arg(long)]
    check_counter: bool,
    #[arg(long, default_value = "16777216")]
    rx_buf: usize,
    #[arg(long, default_value = "1048576")]
    tx_buf: usize,
}

struct Stats {
    start: Instant,
    last_report: Instant,
    last_frames: u64,
    last_bytes: u64,
    total_frames: u64,
    total_bytes: u64,
}

impl Stats {
    fn new() -> Self {
        let now = Instant::now();
        Self {
            start: now,
            last_report: now,
            last_frames: 0,
            last_bytes: 0,
            total_frames: 0,
            total_bytes: 0,
        }
    }

    fn add(&mut self, frames: u64, bytes: u64) {
        self.total_frames += frames;
        self.total_bytes += bytes;
    }

    fn report_now(&mut self, label: &str, extra: Option<&str>) {
        self.report_at(label, extra, Instant::now());
    }

    fn maybe_report(&mut self, interval: Duration, label: &str, extra: Option<&str>) {
        let now = Instant::now();
        if now.duration_since(self.last_report) < interval {
            return;
        }
        self.report_at(label, extra, now);
    }

    fn report_at(&mut self, label: &str, extra: Option<&str>, now: Instant) {
        let elapsed = now.duration_since(self.last_report);
        let frames = self.total_frames - self.last_frames;
        let bytes = self.total_bytes - self.last_bytes;
        let secs = elapsed.as_secs_f64();
        let fps = if secs > 0.0 { frames as f64 / secs } else { 0.0 };
        let mbps = if secs > 0.0 {
            (bytes as f64 * 8.0 / 1_000_000.0) / secs
        } else {
            0.0
        };
        if let Some(extra) = extra {
            eprintln!(
                "{label}: total={} fps={:.0} Mbps={:.3} {extra}",
                self.total_frames, fps, mbps
            );
        } else {
            eprintln!(
                "{label}: total={} fps={:.0} Mbps={:.3}",
                self.total_frames, fps, mbps
            );
        }
        self.last_report = now;
        self.last_frames = self.total_frames;
        self.last_bytes = self.total_bytes;
    }
}

struct RunningStats {
    count: u64,
    min: f64,
    max: f64,
    sum: f64,
}

impl RunningStats {
    fn new() -> Self {
        Self {
            count: 0,
            min: f64::INFINITY,
            max: f64::NEG_INFINITY,
            sum: 0.0,
        }
    }

    fn add(&mut self, value: f64) {
        self.count += 1;
        if value < self.min {
            self.min = value;
        }
        if value > self.max {
            self.max = value;
        }
        self.sum += value;
    }

    fn avg(&self) -> f64 {
        if self.count == 0 {
            0.0
        } else {
            self.sum / self.count as f64
        }
    }
}

enum QualityParse {
    NotQuality,
    Invalid,
    Valid {
        seq: u16,
        sender_offset_ms: u16,
    },
}

struct FdGuard(RawFd);

impl Drop for FdGuard {
    fn drop(&mut self) {
        unsafe {
            libc::close(self.0);
        }
    }
}

fn main() {
    let cli = Cli::parse();
    let result = match cli.command {
        Command::Sender(opts) => run_sender(opts),
        Command::Receiver(opts) => run_receiver(opts),
    };
    if let Err(err) = result {
        eprintln!("error: {err}");
        std::process::exit(1);
    }
}

fn run_sender(opts: SenderOpts) -> Result<(), String> {
    if opts.id > CAN_EFF_MASK {
        return Err(format!("id out of range: 0x{:X}", opts.id));
    }
    if opts.loop_count == 0 {
        return Err("--loop must be >= 1".to_string());
    }
    if let Some(delay_ms) = opts.delay_ms {
        if !(0.0..=1000.0).contains(&delay_ms) {
            return Err("--delay-ms must be between 0 and 1000".to_string());
        }
        if opts.rate > 0 {
            return Err("--delay-ms and --rate are mutually exclusive".to_string());
        }
    }
    if opts.quality_test {
        if opts.len.is_some() {
            return Err("--len is not allowed with --quality-test".to_string());
        }
        if opts.data.is_some() {
            return Err("--data is not allowed with --quality-test".to_string());
        }
        if opts.counter {
            return Err("--counter is not allowed with --quality-test".to_string());
        }
    }
    let max_len = if opts.fd { 64 } else { 8 };
    let (payload, payload_len) = if opts.quality_test {
        (vec![0u8; 8], 8)
    } else {
        let (payload, payload_len) = build_payload(max_len, opts.len, opts.data, opts.fill)?;
        if opts.counter && payload_len < 4 {
            return Err("counter requires len >= 4".to_string());
        }
        (payload, payload_len)
    };

    let can_id = build_can_id(opts.id, opts.extended);
    let fd = open_socket(
        &opts.iface,
        opts.fd,
        opts.rx_buf,
        opts.tx_buf,
        opts.no_loopback,
        opts.recv_own,
        None,
        None,
    )?;
    let _fd_guard = FdGuard(fd);

    let mut stats = Stats::new();
    let stats_interval = if opts.quiet || opts.stats_interval == 0 {
        None
    } else {
        Some(Duration::from_secs(opts.stats_interval))
    };

    let duration = opts.duration.map(Duration::from_secs);
    let total_limit = apply_loop_count(opts.count, opts.loop_count)?;
    let mut counter: u32 = 0;
    let mut quality_seq: u16 = 0;
    let mut tx_drops: u64 = 0;
    let quality_start = Instant::now();

    let nanos_per_frame = if let Some(delay_ms) = opts.delay_ms {
        if delay_ms <= 0.0 {
            0
        } else {
            (delay_ms * 1_000_000.0).round() as u64
        }
    } else if opts.rate > 0 {
        1_000_000_000u64 / opts.rate
    } else {
        0
    };
    let mut next_send = Instant::now();

    if opts.fd {
        let mut frame = CanFdFrame {
            can_id,
            len: payload_len as u8,
            flags: 0,
            __res0: 0,
            __res1: 0,
            data: [opts.fill; 64],
        };
        frame.data[..payload_len].copy_from_slice(&payload[..payload_len]);
        send_loop_fd(
            fd,
            &mut frame,
            payload_len,
            opts.counter,
            &mut counter,
            opts.quality_test,
            &mut quality_seq,
            quality_start,
            opts.test_id,
            total_limit,
            duration,
            nanos_per_frame,
            &mut next_send,
            &mut stats,
            stats_interval,
            &mut tx_drops,
        )?;
    } else {
        let mut frame = CanFrame {
            can_id,
            can_dlc: payload_len as u8,
            __pad: 0,
            __res0: 0,
            __res1: 0,
            data: [opts.fill; 8],
        };
        frame.data[..payload_len].copy_from_slice(&payload[..payload_len]);
        send_loop_std(
            fd,
            &mut frame,
            payload_len,
            opts.counter,
            &mut counter,
            opts.quality_test,
            &mut quality_seq,
            quality_start,
            opts.test_id,
            total_limit,
            duration,
            nanos_per_frame,
            &mut next_send,
            &mut stats,
            stats_interval,
            &mut tx_drops,
        )?;
    }

    if !opts.quiet {
        if tx_drops > 0 {
            eprintln!(
                "tx: done total={} drops={} elapsed={:.3}s",
                stats.total_frames,
                tx_drops,
                stats.start.elapsed().as_secs_f64()
            );
        } else {
            eprintln!(
                "tx: done total={} elapsed={:.3}s",
                stats.total_frames,
                stats.start.elapsed().as_secs_f64()
            );
        }
    }
    Ok(())
}

#[allow(clippy::too_many_arguments)]
fn send_loop_std(
    fd: RawFd,
    frame: &mut CanFrame,
    payload_len: usize,
    use_counter: bool,
    counter: &mut u32,
    quality_test: bool,
    quality_seq: &mut u16,
    quality_start: Instant,
    quality_test_id: u8,
    limit: u64,
    duration: Option<Duration>,
    nanos_per_frame: u64,
    next_send: &mut Instant,
    stats: &mut Stats,
    stats_interval: Option<Duration>,
    tx_drops: &mut u64,
) -> Result<(), String> {
    loop {
        if quality_test {
            fill_quality_test_payload(&mut frame.data, *quality_seq, quality_start, quality_test_id);
            *quality_seq = quality_seq.wrapping_add(1);
        } else if use_counter {
            let bytes = counter.to_le_bytes();
            frame.data[0..4].copy_from_slice(&bytes);
            *counter = counter.wrapping_add(1);
        }

        send_frame(fd, frame as *const _ as *const u8, mem::size_of::<CanFrame>(), tx_drops)?;
        stats.add(1, payload_len as u64);

        if limit > 0 && stats.total_frames >= limit {
            break;
        }
        if let Some(d) = duration {
            if stats.start.elapsed() >= d {
                break;
            }
        }

        if nanos_per_frame > 0 {
            throttle(nanos_per_frame, next_send);
        }

        if let Some(interval) = stats_interval {
            if *tx_drops > 0 {
                let extra = format!("drops={}", *tx_drops);
                stats.maybe_report(interval, "tx", Some(extra.as_str()));
            } else {
                stats.maybe_report(interval, "tx", None);
            }
        }
    }
    Ok(())
}

#[allow(clippy::too_many_arguments)]
fn send_loop_fd(
    fd: RawFd,
    frame: &mut CanFdFrame,
    payload_len: usize,
    use_counter: bool,
    counter: &mut u32,
    quality_test: bool,
    quality_seq: &mut u16,
    quality_start: Instant,
    quality_test_id: u8,
    limit: u64,
    duration: Option<Duration>,
    nanos_per_frame: u64,
    next_send: &mut Instant,
    stats: &mut Stats,
    stats_interval: Option<Duration>,
    tx_drops: &mut u64,
) -> Result<(), String> {
    loop {
        if quality_test {
            fill_quality_test_payload(&mut frame.data, *quality_seq, quality_start, quality_test_id);
            *quality_seq = quality_seq.wrapping_add(1);
        } else if use_counter {
            let bytes = counter.to_le_bytes();
            frame.data[0..4].copy_from_slice(&bytes);
            *counter = counter.wrapping_add(1);
        }

        send_frame(
            fd,
            frame as *const _ as *const u8,
            mem::size_of::<CanFdFrame>(),
            tx_drops,
        )?;
        stats.add(1, payload_len as u64);

        if limit > 0 && stats.total_frames >= limit {
            break;
        }
        if let Some(d) = duration {
            if stats.start.elapsed() >= d {
                break;
            }
        }

        if nanos_per_frame > 0 {
            throttle(nanos_per_frame, next_send);
        }

        if let Some(interval) = stats_interval {
            if *tx_drops > 0 {
                let extra = format!("drops={}", *tx_drops);
                stats.maybe_report(interval, "tx", Some(extra.as_str()));
            } else {
                stats.maybe_report(interval, "tx", None);
            }
        }
    }
    Ok(())
}

fn throttle(nanos_per_frame: u64, next_send: &mut Instant) {
    if nanos_per_frame == 0 {
        return;
    }
    let now = Instant::now();
    if now < *next_send {
        let delay = *next_send - now;
        if delay > Duration::from_micros(200) {
            std::thread::sleep(delay);
        } else {
            while Instant::now() < *next_send {
                std::hint::spin_loop();
            }
        }
    }
    let base = if now > *next_send { now } else { *next_send };
    *next_send = base + Duration::from_nanos(nanos_per_frame);
}

fn run_receiver(opts: ReceiverOpts) -> Result<(), String> {
    let max_len = if opts.fd { 64 } else { 8 };
    if opts.mask.is_some() && opts.id.is_none() {
        return Err("mask requires id".to_string());
    }
    if opts.quality_test && opts.check_counter {
        return Err("--check-counter is not allowed with --quality-test".to_string());
    }
    if let Some(id) = opts.id {
        if id > CAN_EFF_MASK {
            return Err(format!("id out of range: 0x{:X}", id));
        }
    }
    let filter = build_filter(opts.id, opts.mask, opts.extended);
    let idle_exit_ms = if opts.idle_exit_ms == 0 {
        None
    } else {
        Some(opts.idle_exit_ms)
    };
    let recv_timeout_ms = idle_exit_ms.map(|ms| ms.min(200));
    let fd = open_socket(
        &opts.iface,
        opts.fd,
        opts.rx_buf,
        opts.tx_buf,
        false,
        false,
        recv_timeout_ms,
        filter,
    )?;
    let _fd_guard = FdGuard(fd);

    let mut stats = Stats::new();
    let stats_interval = if opts.quiet || opts.stats_interval == 0 {
        None
    } else {
        Some(Duration::from_secs(opts.stats_interval))
    };
    let duration = opts.duration.map(Duration::from_secs);

    let mut last_counter: Option<u32> = None;
    let mut counter_drops: u64 = 0;
    let mut counter_ooo: u64 = 0;
    let mut qt_last_seq: Option<u16> = None;
    let mut qt_drops: u64 = 0;
    let mut qt_ooo: u64 = 0;
    let mut qt_invalid: u64 = 0;
    let mut qt_valid: u64 = 0;
    let mut qt_interarrival = RunningStats::new();
    let mut qt_jitter = RunningStats::new();
    let mut qt_last_rx: Option<Instant> = None;
    let mut qt_interarrival_sum = 0.0;
    let mut qt_interarrival_count: u64 = 0;
    let mut last_rx: Option<Instant> = None;

    if opts.fd {
        let mut frame = CanFdFrame {
            can_id: 0,
            len: 0,
            flags: 0,
            __res0: 0,
            __res1: 0,
            data: [0; 64],
        };
        loop {
            let read = recv_frame(fd, &mut frame as *mut _ as *mut u8, mem::size_of::<CanFdFrame>())?;
            if read == 0 {
                if let Some(idle_ms) = idle_exit_ms {
                    if let Some(last) = last_rx {
                        if last.elapsed().as_millis() as u64 >= idle_ms {
                            if !opts.quiet {
                                let extra = if opts.quality_test {
                                    let ia = format_triplet(&qt_interarrival);
                                    let jit = format_triplet(&qt_jitter);
                                    Some(format!(
                                        "qt_valid={} qt_invalid={} drops={} ooo={} ia_ms={} jit_ms={}",
                                        qt_valid, qt_invalid, qt_drops, qt_ooo, ia, jit
                                    ))
                                } else if opts.check_counter {
                                    Some(format!("drops={} ooo={}", counter_drops, counter_ooo))
                                } else {
                                    None
                                };
                                if let Some(extra) = extra.as_deref() {
                                    stats.report_now("rx", Some(extra));
                                } else {
                                    stats.report_now("rx", None);
                                }
                            }
                            break;
                        }
                    }
                }
                continue;
            }
            let len = (frame.len as usize).min(max_len);
            if opts.quality_test {
                match parse_quality_test(&frame.data[..len]) {
                    QualityParse::NotQuality => {
                        continue;
                    }
                    QualityParse::Invalid => {
                        qt_invalid += 1;
                        if opts.dump {
                            dump_frame(frame.can_id, &frame.data[..len]);
                        }
                        continue;
                    }
                    QualityParse::Valid {
                        seq,
                        sender_offset_ms: _sender_offset_ms,
                    } => {
                        if let Some(test_id) = opts.test_id {
                            if frame.data[6] != test_id {
                                continue;
                            }
                        }
                        if opts.dump {
                            dump_frame(frame.can_id, &frame.data[..len]);
                        }
                        stats.add(1, len as u64);
                        qt_valid += 1;
                        update_u16_stats(seq, &mut qt_last_seq, &mut qt_drops, &mut qt_ooo);

                        let now = Instant::now();
                        if let Some(last) = qt_last_rx {
                            let delta_ms = now.duration_since(last).as_secs_f64() * 1000.0;
                            qt_interarrival.add(delta_ms);
                            qt_interarrival_sum += delta_ms;
                            qt_interarrival_count += 1;
                            let avg_ia = qt_interarrival_sum / qt_interarrival_count as f64;
                            let jitter_ms = (delta_ms - avg_ia).abs();
                            qt_jitter.add(jitter_ms);
                        }
                        qt_last_rx = Some(now);
                        last_rx = Some(now);
                    }
                }
            } else {
                stats.add(1, len as u64);

                if opts.dump {
                    dump_frame(frame.can_id, &frame.data[..len]);
                }

                if opts.check_counter && len >= 4 {
                    let counter = u32::from_le_bytes([
                        frame.data[0],
                        frame.data[1],
                        frame.data[2],
                        frame.data[3],
                    ]);
                    update_counter_stats(counter, &mut last_counter, &mut counter_drops, &mut counter_ooo);
                }
                last_rx = Some(Instant::now());
            }

            if let Some(interval) = stats_interval {
                let extra = if opts.quality_test {
                    let ia = format_triplet(&qt_interarrival);
                    let jit = format_triplet(&qt_jitter);
                    Some(format!(
                        "qt_valid={} qt_invalid={} drops={} ooo={} ia_ms={} jit_ms={}",
                        qt_valid, qt_invalid, qt_drops, qt_ooo, ia, jit
                    ))
                } else if opts.check_counter {
                    Some(format!("drops={} ooo={}", counter_drops, counter_ooo))
                } else {
                    None
                };
                if let Some(extra) = extra.as_deref() {
                    stats.maybe_report(interval, "rx", Some(extra));
                } else {
                    stats.maybe_report(interval, "rx", None);
                }
            }

            if opts.count > 0 && stats.total_frames >= opts.count {
                break;
            }
            if let Some(d) = duration {
                if stats.start.elapsed() >= d {
                    break;
                }
            }
        }
    } else {
        let mut frame = CanFrame {
            can_id: 0,
            can_dlc: 0,
            __pad: 0,
            __res0: 0,
            __res1: 0,
            data: [0; 8],
        };
        loop {
            let read = recv_frame(fd, &mut frame as *mut _ as *mut u8, mem::size_of::<CanFrame>())?;
            if read == 0 {
                if let Some(idle_ms) = idle_exit_ms {
                    if let Some(last) = last_rx {
                        if last.elapsed().as_millis() as u64 >= idle_ms {
                            if !opts.quiet {
                                let extra = if opts.quality_test {
                                    let ia = format_triplet(&qt_interarrival);
                                    let jit = format_triplet(&qt_jitter);
                                    Some(format!(
                                        "qt_valid={} qt_invalid={} drops={} ooo={} ia_ms={} jit_ms={}",
                                        qt_valid, qt_invalid, qt_drops, qt_ooo, ia, jit
                                    ))
                                } else if opts.check_counter {
                                    Some(format!("drops={} ooo={}", counter_drops, counter_ooo))
                                } else {
                                    None
                                };
                                if let Some(extra) = extra.as_deref() {
                                    stats.report_now("rx", Some(extra));
                                } else {
                                    stats.report_now("rx", None);
                                }
                            }
                            break;
                        }
                    }
                }
                continue;
            }
            let len = (frame.can_dlc as usize).min(max_len);
            if opts.quality_test {
                match parse_quality_test(&frame.data[..len]) {
                    QualityParse::NotQuality => {
                        continue;
                    }
                    QualityParse::Invalid => {
                        qt_invalid += 1;
                        if opts.dump {
                            dump_frame(frame.can_id, &frame.data[..len]);
                        }
                        continue;
                    }
                    QualityParse::Valid {
                        seq,
                        sender_offset_ms: _sender_offset_ms,
                    } => {
                        if let Some(test_id) = opts.test_id {
                            if frame.data[6] != test_id {
                                continue;
                            }
                        }
                        if opts.dump {
                            dump_frame(frame.can_id, &frame.data[..len]);
                        }
                        stats.add(1, len as u64);
                        qt_valid += 1;
                        update_u16_stats(seq, &mut qt_last_seq, &mut qt_drops, &mut qt_ooo);

                        let now = Instant::now();
                        if let Some(last) = qt_last_rx {
                            let delta_ms = now.duration_since(last).as_secs_f64() * 1000.0;
                            qt_interarrival.add(delta_ms);
                            qt_interarrival_sum += delta_ms;
                            qt_interarrival_count += 1;
                            let avg_ia = qt_interarrival_sum / qt_interarrival_count as f64;
                            let jitter_ms = (delta_ms - avg_ia).abs();
                            qt_jitter.add(jitter_ms);
                        }
                        qt_last_rx = Some(now);
                        last_rx = Some(now);
                    }
                }
            } else {
                stats.add(1, len as u64);

                if opts.dump {
                    dump_frame(frame.can_id, &frame.data[..len]);
                }

                if opts.check_counter && len >= 4 {
                    let counter = u32::from_le_bytes([
                        frame.data[0],
                        frame.data[1],
                        frame.data[2],
                        frame.data[3],
                    ]);
                    update_counter_stats(counter, &mut last_counter, &mut counter_drops, &mut counter_ooo);
                }
                last_rx = Some(Instant::now());
            }

            if let Some(interval) = stats_interval {
                let extra = if opts.quality_test {
                    let ia = format_triplet(&qt_interarrival);
                    let jit = format_triplet(&qt_jitter);
                    Some(format!(
                        "qt_valid={} qt_invalid={} drops={} ooo={} ia_ms={} jit_ms={}",
                        qt_valid, qt_invalid, qt_drops, qt_ooo, ia, jit
                    ))
                } else if opts.check_counter {
                    Some(format!("drops={} ooo={}", counter_drops, counter_ooo))
                } else {
                    None
                };
                if let Some(extra) = extra.as_deref() {
                    stats.maybe_report(interval, "rx", Some(extra));
                } else {
                    stats.maybe_report(interval, "rx", None);
                }
            }

            if opts.count > 0 && stats.total_frames >= opts.count {
                break;
            }
            if let Some(d) = duration {
                if stats.start.elapsed() >= d {
                    break;
                }
            }
        }
    }

    if !opts.quiet {
        if opts.quality_test {
            let ia = format_triplet(&qt_interarrival);
            let jit = format_triplet(&qt_jitter);
            eprintln!(
                "rx: done total={} qt_valid={} qt_invalid={} drops={} ooo={} ia_ms={} jit_ms={} elapsed={:.3}s",
                stats.total_frames,
                qt_valid,
                qt_invalid,
                qt_drops,
                qt_ooo,
                ia,
                jit,
                stats.start.elapsed().as_secs_f64()
            );
        } else if opts.check_counter {
            eprintln!(
                "rx: done total={} drops={} ooo={} elapsed={:.3}s",
                stats.total_frames,
                counter_drops,
                counter_ooo,
                stats.start.elapsed().as_secs_f64()
            );
        } else {
            eprintln!(
                "rx: done total={} elapsed={:.3}s",
                stats.total_frames,
                stats.start.elapsed().as_secs_f64()
            );
        }
    }
    Ok(())
}

fn send_frame(fd: RawFd, buf: *const u8, len: usize, drops: &mut u64) -> Result<(), String> {
    loop {
        let ret = unsafe { libc::write(fd, buf as *const _, len) };
        if ret >= 0 {
            return Ok(());
        }
        let err = std::io::Error::last_os_error();
        match err.raw_os_error() {
            Some(libc::EINTR) => continue,
            Some(libc::ENOBUFS) => {
                *drops += 1;
                return Ok(());
            }
            _ => return Err(format!("send failed: {err}")),
        }
    }
}

fn recv_frame(fd: RawFd, buf: *mut u8, len: usize) -> Result<usize, String> {
    loop {
        let ret = unsafe { libc::read(fd, buf as *mut _, len) };
        if ret >= 0 {
            return Ok(ret as usize);
        }
        let err = std::io::Error::last_os_error();
        match err.raw_os_error() {
            Some(libc::EINTR) => continue,
            Some(code) if code == libc::EAGAIN || code == libc::EWOULDBLOCK => return Ok(0),
            _ => return Err(format!("recv failed: {err}")),
        }
    }
}

fn open_socket(
    iface: &str,
    fd_frames: bool,
    rx_buf: usize,
    tx_buf: usize,
    no_loopback: bool,
    recv_own: bool,
    recv_timeout_ms: Option<u64>,
    filter: Option<CanFilter>,
) -> Result<RawFd, String> {
    let fd = unsafe { libc::socket(libc::AF_CAN, libc::SOCK_RAW, libc::CAN_RAW) };
    if fd < 0 {
        return Err(format!(
            "socket(AF_CAN) failed: {}",
            std::io::Error::last_os_error()
        ));
    }

    if rx_buf > 0 {
        set_sockopt_int(fd, libc::SOL_SOCKET, libc::SO_RCVBUF, rx_buf)?;
    }
    if tx_buf > 0 {
        set_sockopt_int(fd, libc::SOL_SOCKET, libc::SO_SNDBUF, tx_buf)?;
    }

    if fd_frames {
        set_sockopt_int(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1)?;
    }
    if no_loopback {
        set_sockopt_int(fd, SOL_CAN_RAW, CAN_RAW_LOOPBACK, 0)?;
    }
    if recv_own {
        set_sockopt_int(fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, 1)?;
    }
    if let Some(timeout_ms) = recv_timeout_ms {
        if timeout_ms > 0 {
            set_sockopt_timeval(fd, libc::SOL_SOCKET, libc::SO_RCVTIMEO, timeout_ms)?;
        }
    }
    if let Some(filter) = filter {
        set_sockopt_filter(fd, filter)?;
    }

    let c_iface = CString::new(iface).map_err(|_| "iface contains NUL")?;
    let ifindex = unsafe { libc::if_nametoindex(c_iface.as_ptr()) };
    if ifindex == 0 {
        return Err(format!("unknown interface: {iface}"));
    }

    let mut addr: libc::sockaddr_can = unsafe { mem::zeroed() };
    addr.can_family = libc::AF_CAN as libc::sa_family_t;
    addr.can_ifindex = ifindex as libc::c_int;

    let ret = unsafe {
        libc::bind(
            fd,
            &addr as *const _ as *const libc::sockaddr,
            mem::size_of::<libc::sockaddr_can>() as libc::socklen_t,
        )
    };
    if ret != 0 {
        return Err(format!("bind failed: {}", std::io::Error::last_os_error()));
    }

    Ok(fd)
}

fn set_sockopt_int(fd: RawFd, level: i32, opt: i32, val: usize) -> Result<(), String> {
    let val: libc::c_int = val
        .try_into()
        .map_err(|_| format!("sockopt value too large: {val}"))?;
    let ret = unsafe {
        libc::setsockopt(
            fd,
            level,
            opt,
            &val as *const _ as *const _,
            mem::size_of_val(&val) as libc::socklen_t,
        )
    };
    if ret != 0 {
        return Err(format!(
            "setsockopt({opt}) failed: {}",
            std::io::Error::last_os_error()
        ));
    }
    Ok(())
}

fn set_sockopt_filter(fd: RawFd, filter: CanFilter) -> Result<(), String> {
    let ret = unsafe {
        libc::setsockopt(
            fd,
            SOL_CAN_RAW,
            CAN_RAW_FILTER,
            &filter as *const _ as *const _,
            mem::size_of_val(&filter) as libc::socklen_t,
        )
    };
    if ret != 0 {
        return Err(format!(
            "setsockopt(CAN_RAW_FILTER) failed: {}",
            std::io::Error::last_os_error()
        ));
    }
    Ok(())
}

fn set_sockopt_timeval(fd: RawFd, level: i32, opt: i32, ms: u64) -> Result<(), String> {
    let secs = (ms / 1000) as libc::time_t;
    let usecs = ((ms % 1000) * 1000) as libc::suseconds_t;
    let tv = libc::timeval {
        tv_sec: secs,
        tv_usec: usecs,
    };
    let ret = unsafe {
        libc::setsockopt(
            fd,
            level,
            opt,
            &tv as *const _ as *const _,
            mem::size_of_val(&tv) as libc::socklen_t,
        )
    };
    if ret != 0 {
        return Err(format!(
            "setsockopt({opt}) failed: {}",
            std::io::Error::last_os_error()
        ));
    }
    Ok(())
}

fn build_payload(
    max_len: usize,
    len: Option<usize>,
    data: Option<String>,
    fill: u8,
) -> Result<(Vec<u8>, usize), String> {
    let mut payload = if let Some(data) = data {
        parse_hex_bytes(&data)?
    } else {
        Vec::new()
    };

    let payload_len = match len {
        Some(len) => len,
        None => {
            if !payload.is_empty() {
                payload.len()
            } else {
                8
            }
        }
    };

    if payload_len > max_len {
        return Err(format!("len {} exceeds max {}", payload_len, max_len));
    }

    if payload.len() < payload_len {
        payload.resize(payload_len, fill);
    } else if payload.len() > payload_len {
        payload.truncate(payload_len);
    }

    Ok((payload, payload_len))
}

fn build_can_id(id: u32, extended: bool) -> u32 {
    let mut can_id = id;
    if extended || id > CAN_SFF_MASK {
        can_id |= CAN_EFF_FLAG;
    }
    can_id
}

fn build_filter(id: Option<u32>, mask: Option<u32>, extended: bool) -> Option<CanFilter> {
    let id = id?;
    let mut can_id = id;
    if extended || id > CAN_SFF_MASK {
        can_id |= CAN_EFF_FLAG;
    }
    let mut can_mask = mask.unwrap_or_else(|| {
        if extended || id > CAN_SFF_MASK {
            CAN_EFF_MASK
        } else {
            CAN_SFF_MASK
        }
    });
    if mask.is_none() {
        can_mask |= CAN_EFF_FLAG;
    }
    Some(CanFilter { can_id, can_mask })
}

fn update_counter_stats(
    counter: u32,
    last: &mut Option<u32>,
    drops: &mut u64,
    ooo: &mut u64,
) {
    if let Some(prev) = *last {
        if counter == prev.wrapping_add(1) {
            *last = Some(counter);
        } else if counter > prev {
            *drops += (counter - prev - 1) as u64;
            *last = Some(counter);
        } else {
            *ooo += 1;
            *last = Some(counter);
        }
    } else {
        *last = Some(counter);
    }
}

fn update_u16_stats(counter: u16, last: &mut Option<u16>, drops: &mut u64, ooo: &mut u64) {
    if let Some(prev) = *last {
        let expected = prev.wrapping_add(1);
        if counter == expected {
            *last = Some(counter);
            return;
        }
        let distance = counter.wrapping_sub(expected) as u16;
        if distance < 0x8000 {
            *drops += distance as u64;
            *last = Some(counter);
        } else {
            *ooo += 1;
            *last = Some(counter);
        }
    } else {
        *last = Some(counter);
    }
}

fn fill_quality_test_payload(buf: &mut [u8], seq: u16, start: Instant, test_id: u8) {
    if buf.len() < 8 {
        return;
    }
    let ts_offset = (start.elapsed().as_millis() as u64) & 0xFFFF;
    buf[0] = 0xCA;
    buf[1] = 0xFE;
    buf[2] = (seq >> 8) as u8;
    buf[3] = (seq & 0xFF) as u8;
    buf[4] = ((ts_offset >> 8) & 0xFF) as u8;
    buf[5] = (ts_offset & 0xFF) as u8;
    buf[6] = test_id;
    let mut checksum = 0u8;
    for byte in buf.iter().take(7) {
        checksum ^= *byte;
    }
    buf[7] = checksum;
}

fn parse_quality_test(data: &[u8]) -> QualityParse {
    if data.len() < 8 {
        return QualityParse::NotQuality;
    }
    if data[0] != 0xCA || data[1] != 0xFE {
        return QualityParse::NotQuality;
    }
    let mut checksum = 0u8;
    for byte in data.iter().take(7) {
        checksum ^= *byte;
    }
    if checksum != data[7] {
        return QualityParse::Invalid;
    }
    let seq = ((data[2] as u16) << 8) | data[3] as u16;
    let sender_offset_ms = ((data[4] as u16) << 8) | data[5] as u16;
    QualityParse::Valid { seq, sender_offset_ms }
}

fn dump_frame(can_id: u32, data: &[u8]) {
    let is_eff = (can_id & CAN_EFF_FLAG) != 0;
    let id = if is_eff {
        can_id & CAN_EFF_MASK
    } else {
        can_id & CAN_SFF_MASK
    };
    if (can_id & CAN_ERR_FLAG) != 0 {
        print!("ERR ");
    } else if (can_id & CAN_RTR_FLAG) != 0 {
        print!("RTR ");
    }
    if is_eff {
        print!("{id:08X} ");
    } else {
        print!("{id:03X} ");
    }
    print!("[{}] ", data.len());
    for (idx, byte) in data.iter().enumerate() {
        if idx > 0 {
            print!(" ");
        }
        print!("{byte:02X}");
    }
    println!();
}

fn format_triplet(stats: &RunningStats) -> String {
    if stats.count == 0 {
        "n/a".to_string()
    } else {
        format!("{:.3}/{:.3}/{:.3}", stats.min, stats.avg(), stats.max)
    }
}

fn apply_loop_count(count: u64, loop_count: u64) -> Result<u64, String> {
    if loop_count <= 1 {
        return Ok(count);
    }
    if count == 0 {
        return Err("--loop requires --count".to_string());
    }
    count
        .checked_mul(loop_count)
        .ok_or_else(|| "count * loop overflows".to_string())
}

fn parse_u64(input: &str) -> Result<u64, String> {
    parse_int_u64(input)
}

fn parse_f64(input: &str) -> Result<f64, String> {
    input.trim().parse::<f64>().map_err(|e| e.to_string())
}

fn parse_u32(input: &str) -> Result<u32, String> {
    let value = parse_int_u64(input)?;
    value
        .try_into()
        .map_err(|_| format!("value out of range: {value}"))
}

fn parse_u8(input: &str) -> Result<u8, String> {
    let value = parse_int_u64(input)?;
    value
        .try_into()
        .map_err(|_| format!("value out of range: {value}"))
}

fn parse_int_u64(input: &str) -> Result<u64, String> {
    let trimmed = input.trim();
    if let Some(hex) = trimmed.strip_prefix("0x").or_else(|| trimmed.strip_prefix("0X")) {
        u64::from_str_radix(hex, 16).map_err(|e| e.to_string())
    } else {
        trimmed.parse::<u64>().map_err(|e| e.to_string())
    }
}

fn parse_hex_bytes(input: &str) -> Result<Vec<u8>, String> {
    let trimmed = input.trim();
    if trimmed.is_empty() {
        return Ok(Vec::new());
    }

    let mut bytes = Vec::new();
    let has_separators = trimmed
        .chars()
        .any(|c| c.is_whitespace() || c == ',' || c == ':' || c == '-');
    if has_separators {
        for token in trimmed.split(|c: char| c.is_whitespace() || c == ',' || c == ':' || c == '-') {
            if token.is_empty() {
                continue;
            }
            let token = token.strip_prefix("0x").or_else(|| token.strip_prefix("0X")).unwrap_or(token);
            if token.len() == 0 {
                continue;
            }
            if token.len() == 2 {
                let byte = u8::from_str_radix(token, 16).map_err(|e| e.to_string())?;
                bytes.push(byte);
            } else if token.len() % 2 == 0 {
                for i in (0..token.len()).step_by(2) {
                    let byte = u8::from_str_radix(&token[i..i + 2], 16)
                        .map_err(|e| e.to_string())?;
                    bytes.push(byte);
                }
            } else {
                return Err(format!("invalid hex token: {token}"));
            }
        }
    } else {
        let token = trimmed.strip_prefix("0x").or_else(|| trimmed.strip_prefix("0X")).unwrap_or(trimmed);
        if token.len() % 2 != 0 {
            return Err("hex string must have even length".to_string());
        }
        for i in (0..token.len()).step_by(2) {
            let byte =
                u8::from_str_radix(&token[i..i + 2], 16).map_err(|e| e.to_string())?;
            bytes.push(byte);
        }
    }

    Ok(bytes)
}
