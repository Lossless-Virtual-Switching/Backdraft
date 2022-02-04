from bcc import BPF
import ctypes
import sys


class notify_t:
    _fields_ = [('pid', ctypes.c_uint64),
                ('adv_wnd', ctypes.c_uint32)]


def handle_event(cpu, data, size):
    notify = ctypes.cast(data, ctypes.POINTER(notify_t)).contents
    pid = notify.pid
    window = notify.adv_wnd 
    print(f'{pid}: {window}')


def main():
    source_file = './kprobe.c'
    with open(source_file) as f:
        program = f.read()

    b = BPF(text=program)
    b['output'].open_perf_buffer(handle_event)
    b.trace_print()
    while True:
        try:
            b.kprobe_poll()
        except:
            sys.exit(0)


if __name__ == '__main__':
    main()
