from typing import Dict, List, Tuple
import socket
import sys
import mido
import threading
import pprint
import signal
import re

from debug.control_pb2 import Control, Button

from absl import flags

FLAGS = flags.FLAGS

flags.DEFINE_string("input_filter", None, "")


def make_key(msg):
    control = 0
    note = 0
    if 'control' in msg.dict():
        control = msg.control
    if 'note' in msg.dict():
        note = msg.note
    return f'{msg.type:s}:{msg.channel:d}:{control:d}:{note:d}'


def any_value(msg):
    if 'value' in msg.dict():
        return msg.value
    if 'velocity' in msg.dict():
        return msg.velocity
    if 'pitch' in msg.dict():
        return msg.pitch
    return 0


class Runner:
    def __init__(self):
        self.control_ranges: Dict[str, Tuple[int, int]] = {}
        self.control_values: Dict[str, float] = {}
        self.sorted_control_keys: List[str] = []
        self.buttons: List[Button] = []

        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.server_tuple = ('127.0.0.1', 9944)

    def start(self):
        self.should_exit = False
        self.thread = threading.Thread(target=self.run)
        self.thread.start()

    def update_value(self, key, value):
        ranges = None
        if key not in self.control_ranges:
            self.control_ranges[key] = (value, value)
        else:
            ranges = self.control_ranges[key]
            if value < ranges[0]:
                ranges = (value, ranges[1])
            if value > ranges[1]:
                ranges = (ranges[0], value)
            self.control_ranges[key] = ranges

        if (ranges is None) or (ranges[0] == ranges[1]):
            self.control_values[key] = 0
            return

        self.control_values[key] = (value - ranges[0]) / (ranges[1] -
                                                          ranges[0])

    def maybe_hash_value(self, msg):
        key = make_key(msg)
        if key not in self.sorted_control_keys:
            self.sorted_control_keys.append(key)
            self.sorted_control_keys = sorted(self.sorted_control_keys)
        self.update_value(key, any_value(msg))

    def stop(self):
        self.should_exit = True
        self.thread.join()

    def send_control(self):
        control = Control()
        for k, v in self.control_values.items():
            control.control[k] = v
        control.buttons.extend(self.buttons)
        self.buttons = []
        # print(control)
        self.socket.sendto(control.SerializeToString(), self.server_tuple)

    def initialize_device(self):
        input_names = mido.get_input_names()

        print("All inputs:", input_names)

        input_name = list(
            filter((lambda name: re.match(FLAGS.input_filter, name)),
                   input_names))[0]

        print(f"Opening input: \"{input_name:s}\"")

        return mido.open_input(input_name)

    def run(self):
        inport = self.initialize_device()

        while not self.should_exit:
            msg = inport.receive()
            if msg.type == 'reset':
                continue
            if msg.type.startswith('note'):
                button = Button()
                button.channel = msg.channel * 1000 + msg.note
                button.state = (Button.State.PRESSED if msg.type == 'note_on'
                                else Button.State.RELEASED)
                button.velocity = msg.velocity
                self.buttons.append(button)
            else:
                self.maybe_hash_value(msg)
            # print(
            #     [self.control_values[key] for key in self.sorted_control_keys])
            self.send_control()
        print("Exiting")


runner = Runner()


def start():
    print("Runner start")
    runner.start()


def stop():
    print("Runner stop")
    runner.stop()


def handle_sigint(x, y):
    stop()


if __name__ == '__main__':
    FLAGS(sys.argv)

    signal.signal(signal.SIGINT, handle_sigint)

    start()
