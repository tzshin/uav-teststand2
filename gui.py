# Good GUI design doc: https://www.pysimplegui.org/en/latest/cookbook/

import PySimpleGUI as sg
import serial
import serial.tools.list_ports
import json
import time
import os
import pandas as pd
import matplotlib.pyplot as plt
import shutil
from datetime import datetime

#
# Parameters
#

SYSINIT_TIMEOUT = 25
MEASURE_TIMEOUT = 120

SERIAL_BAUD = 115200

DEFAULT_FONT_SIZE = sg.DEFAULT_FONT[1]
FONT_MONO = ('Courier New', DEFAULT_FONT_SIZE)

#
# Functions
#

# Make the main window.
def make_window(serial_baud ,theme=None):
    sg.theme(theme)
    ports, _, _ = zip(*list(serial.tools.list_ports.comports()))
    layout = [
        [sg.Frame('Serial Setup', [
            [sg.Combo(ports, key="-PORT-", size=10, enable_events=True)],
            [sg.Text(f'Baud: {serial_baud}', font=FONT_MONO)],
            [sg.Text('Serial NOT Connected.', key="-SER STAT-")],
            [sg.Button('Connect', key='-CONNECT-')]
        ])],
        [sg.Frame('Session Info', [
            [sg.Text('Session Name', font=FONT_MONO), sg.Push(), sg.Input(key='-NAME-', size=30)],
            [sg.Text('Resolution', font=FONT_MONO), sg.Push(), sg.Combo([10, 20], key="-RESOLUTION-", size=20, enable_events=True)],
            [sg.Text('Output Scaling', font=FONT_MONO), sg.Push(),
             sg.Slider(range=(0.1, 1.0), default_value=1.0, resolution=0.1, orientation='h', size=(20, 15), key='-OUTPUT SCALE-')],
            [sg.Text('Storage Dir', font=FONT_MONO), sg.Push(),
             sg.Input(key="-FOLDER-", size=20),
             sg.FolderBrowse(key='-BROWSE-')],
            [sg.Button('Lock', key='-LOCK-', disabled=True)]
        ])],
        [sg.Frame('Measuring Control', [
            [sg.Text(f'Establish Serial Comm and Lock the Session Info First!', key='-SESSION STAT-')],
            [sg.Button('Sys Init!', key='-SYS INIT-', disabled=True),
             sg.Button('Measure!', key='-MEASURE-', disabled=True),
             sg.Button('Visualize!', key='-VISUALIZE-', disabled=True),
             sg.Button('Save & Exit!', key='-SAVE N EXIT-', disabled=True)]
        ])],
        [sg.Button('Terminate', key='-TERMINATE-')]
    ]
    window = sg.Window(f'UAV\'s Teststand2 Buddy', layout=layout, disable_close=True)
    
    return window


# Send a command to the teststand controller and wait for the response.
def command(ser, cmd, response_type, timeout):
    ser.write(json.dumps(cmd).encode())
    ser.flush()
    start_t = time.time()

    while True:
        if time.time() - start_t > timeout:
            print('Function command() timeout!')
            return None
        if ser.in_waiting:
            try:
                result = json.loads(ser.readline().decode('utf-8').strip())
                assert result['response_type'] == response_type
                return result
            except:
                continue


# Visualize the collected data.
def visualize(data, measure_param):
    calculated_data = data.copy()
    calculated_data['power'] = calculated_data['voltage'] * calculated_data['current']
    calculated_data['efficiency'] = calculated_data['thrust'] / calculated_data['power']

    throttle = calculated_data['throttle']
    rpm = calculated_data['rpm']
    power = calculated_data['power']
    thrust = calculated_data['thrust']
    current = calculated_data['current']
    efficiency = calculated_data['efficiency']

    fig, axs = plt.subplots(4, 2, figsize=(15, 10))
    set_scale(fig.dpi / 72)

    axs[0, 0].plot(throttle, power)
    axs[0, 0].set_title('Power (w) vs Throttle %')
    axs[0, 1].plot(rpm, power)
    axs[0, 1].set_title('Power (w) vs RPM')

    axs[1, 0].plot(throttle, thrust)
    axs[1, 0].set_title('Thrust (kg) vs Throttle %')
    axs[1, 1].plot(rpm, thrust)
    axs[1, 1].set_title('Thrust (kg) vs RPM')

    axs[2, 0].plot(throttle, current)
    axs[2, 0].set_title('Current (A) vs Throttle %')
    axs[2, 1].plot(rpm, current)
    axs[2, 1].set_title('Current (A) vs RPM')

    axs[3, 0].plot(throttle, efficiency)
    axs[3, 0].set_title('Efficiency (kg/w) vs Throttle %')
    axs[3, 1].plot(rpm, efficiency)
    axs[3, 1].set_title('Efficiency (kg/w) vs RPM')

    title = f"Motor System Performance Analysis - '{measure_param['session_name']}' | Output Scale: {measure_param['output_scale']}"
    description = "Visualization of Power, Thrust, Current, and Efficiency across Throttle and RPM Ranges"
    plt.suptitle(f"{title}\n{description}")
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])

    os.makedirs('./temp', exist_ok=True)
    plt.savefig('./temp/visualized.png')
    plt.show()

    plt.close(fig)


# Fix the window shrinking issue
def set_scale(scale):
    root = sg.tk.Tk()
    root.tk.call('tk', 'scaling', scale)
    root.destroy()

#
# Main
#

window = make_window(serial_baud=SERIAL_BAUD)
window_state = {
    '-PORT-': None,
    '-NAME-': None,
    '-RESOLUTION-': None,
    '-OUTPUT SCALE-': None,
    '-FOLDER-': None
}
data = None
measure_param = None


while True:
    event, values = window.read()


    # Store the serial port information.
    if event == '-PORT-':
        window_state['-PORT-'] = values['-PORT-']


    # Connect to the serial port without validation.
    if event == '-CONNECT-' and window_state['-PORT-'] != None:
        try:
            ser = serial.Serial(window_state['-PORT-'], SERIAL_BAUD)
            ser.write(f'Hello! First time meeting you! I\'m UAV\'s Teststand2 Buddy!'.encode())
            ser.close()
        except:
            print(f'Connecting to {window_state["-PORT-"]} failed.')
            sg.popup_ok(f'Connecting To {window_state["-PORT-"]} Failed.', font=FONT_MONO)
            continue

        window['-CONNECT-'].update(disabled=True)
        window['-PORT-'].update(disabled=True)
        window['-SER STAT-'].update(f'Connected to {window_state["-PORT-"]}!')
        window['-LOCK-'].update(disabled=False)
        print(f'Serial connection {window_state["-PORT-"]} ok!')
        sg.popup_ok(f'Serial Connection {window_state["-PORT-"]} OK!', font=FONT_MONO)


    # Lock the session information.
    if event == '-LOCK-':
        try:
            window_state['-NAME-'] = values['-NAME-']
            window_state['-RESOLUTION-'] = values['-RESOLUTION-']
            window_state['-OUTPUT SCALE-'] = values['-OUTPUT SCALE-']
            window_state['-FOLDER-'] = values['-FOLDER-']
            assert window_state['-NAME-'] != None
            assert window_state['-NAME-'] != ''
            assert window_state['-RESOLUTION-'] != None
            assert os.path.exists(window_state['-FOLDER-'])
        except:
            print('Failed to lock the session information!')
            sg.popup_ok('Failed to Lock the Session Information!', font=FONT_MONO)
            continue
    
        window['-RESOLUTION-'].update(disabled=True)
        window['-OUTPUT SCALE-'].update(disabled=True)
        window['-FOLDER-'].update(disabled=True)
        window['-BROWSE-'].update(disabled=True)
        window['-LOCK-'].update(disabled=True)
        window['-SYS INIT-'].update(disabled=False)
        window['-SESSION STAT-'].update('Initialize the Teststand2 System!')
        print('Session information locked!')
        sg.popup_ok('Session Information Locked!', font=FONT_MONO)


    # Initialize the teststand system.
    if event == '-SYS INIT-':
        m = (
            'Before initializing the system, please ensure:',
            '- The power is correctly connected.',
            '- The prop and motor are correctly mounted.',
            '- There are no people or obstacles around the teststand.',
            'Proceed only if all safety checks are confirmed.'
        )
        if sg.popup(*m, custom_text='Sys Init', font=FONT_MONO, title='Blank Warning') == None:
            continue

        ser = serial.Serial(window_state['-PORT-'], SERIAL_BAUD)
        cmd = {
            'command_type': 'sys_init',
        }
        result = command(ser, cmd, 'sys_init', SYSINIT_TIMEOUT)
        ser.close()

        if result == None:
            print('System initialization timeout!')
            sg.popup_ok('System Initialization Timeout!', font=FONT_MONO)
            continue
        elif result['ok'] == True:
            window['-SYS INIT-'].update(disabled=True)
            window['-MEASURE-'].update(disabled=False)
            window['-SESSION STAT-'].update('System Initialized!')
            print('System initialized!')
            sg.popup_ok('System Initialized!', font=FONT_MONO)
        else:
            print('System initialization failed!')
            sg.popup_ok('System Initialization Failed!', font=FONT_MONO)
            continue


    # Take a measurement.
    if event == '-MEASURE-':
        m = (
            'Before taking a measurement, please ensure:',
            '- The power is correctly connected.',
            '- The prop and motor are correctly mounted.',
            '- There are no people or obstacles around the teststand.',
            'Proceed only if all safety checks are confirmed.'
        )
        if sg.popup(*m, custom_text='Measure', font=FONT_MONO, title='Blank Warning') == None:
            continue

        ser = serial.Serial(window_state['-PORT-'], SERIAL_BAUD)
        cmd = {
            'command_type': 'measure',
            'steps': window_state['-RESOLUTION-'],
            'throttle_scale': window_state['-OUTPUT SCALE-']
        }
        result = command(ser, cmd, 'measure', MEASURE_TIMEOUT)
        ser.close()

        if result == None:
            print('Measurement timeout!')
            sg.popup_ok('Measurement Timeout!', font=FONT_MONO)
            continue
        if result['ok'] == True:
            data = pd.DataFrame(result['data'])
            measure_param = {
                'session_name': window_state['-NAME-'],
                'output_scale': window_state['-OUTPUT SCALE-']
            }
            visualize(data, measure_param)

            window['-MEASURE-'].update(disabled=True)
            window['-VISUALIZE-'].update(disabled=False)
            window['-SAVE N EXIT-'].update(disabled=False)
            window['-SESSION STAT-'].update('Measurement Done!')
            print('The measurement was completed flawlessly :D')
            sg.popup_ok('The Measurement Was Completed Flawlessly :D', font=FONT_MONO)
        else:
            window['-MEASURE-'].update(disabled=True)  # Lock the gui to prevent the user from taking another measurement.
            print(result)
            print('Measurement failed!')
            sg.popup_ok('Measurement Failed!', font=FONT_MONO)
            continue


    # Visualize the collected data.
    if event == '-VISUALIZE-':
        visualize(data, measure_param)


    # Save the collected data and exit.
    if event == '-SAVE N EXIT-':
        storage_dir = f'{window_state["-FOLDER-"]}/{window_state["-NAME-"]}_{datetime.now().strftime("%y%m%d-%H%M")}'
        os.makedirs(storage_dir, exist_ok=True)
        with open(f'{storage_dir}/measure_param.json', 'w') as f:
            json.dump(measure_param, f, indent=4)
        data.to_csv(f'{storage_dir}/data.csv', index=False)
        shutil.copy2('./temp/visualized.png', f'{storage_dir}/visualized.png')
        break


    # Terminate the program without saving.
    if event == '-TERMINATE-':
        m = (
            '********************************************************',
            '********************************************************',
            '**                                                    **',
            '**  Are you sure you want to TERMINATE this program?  **',
            '**  The recorded data will NOT be saved.              **',
            '**                                                    **',
            '********************************************************',
            '********************************************************'
        )
        if sg.popup(*m, custom_text='Terminate', button_color='red', font=FONT_MONO, title='Termination Warning') != None:
            break


window.close()
try:
    ser.close()
except:
    pass