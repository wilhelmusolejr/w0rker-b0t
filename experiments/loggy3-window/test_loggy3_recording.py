import unittest

from loggy3_recording import RecordingUI, action_plan, recording_state


def snapshot(label='Start', **attributes):
    return {'buttons': [{'name': label, 'class': 'btn btn-record btn-primary', 'enabled': True,
                         'offscreen': False, 'invoke_supported': True, **attributes}], 'timers': ['--:--:--']}


class RecordingTests(unittest.TestCase):
    def test_current_start_and_stop_controls_define_state(self):
        self.assertEqual(recording_state(snapshot('Start'))['state'], 'stopped')
        self.assertEqual(recording_state(snapshot('Stop'))['state'], 'recording')
        self.assertEqual(recording_state(snapshot('Stop recording'))['state'], 'recording')

    def test_recording_history_and_unrelated_buttons_are_not_current_status(self):
        data = snapshot('Start')
        data['history'] = ['Recording', 'Stop', '20h 20m']
        self.assertEqual(recording_state(data)['state'], 'stopped')
        self.assertEqual(recording_state(snapshot('Start', **{'class': 'history-start'}))['state'], 'unknown')

    def test_repeated_commands_do_not_toggle_the_recording(self):
        self.assertEqual(action_plan('record', snapshot('Stop'))['action'], 'none')
        self.assertEqual(action_plan('stop', snapshot('Start'))['action'], 'none')

    def test_start_and_stop_are_planned_only_for_the_opposite_known_state(self):
        start = action_plan('record', snapshot('Start'))
        stop = action_plan('stop', snapshot('Stop'))
        self.assertEqual((start['action'], start['expected_name'], start['desired']), ('invoke', 'Start', 'recording'))
        self.assertEqual((stop['action'], stop['expected_name'], stop['desired']), ('invoke', 'Stop', 'stopped'))

    def test_disabled_unknown_hidden_and_conflicting_controls_are_not_invoked(self):
        samples = [snapshot('Start', enabled=False), snapshot('Preparing'),
                   snapshot('Start', offscreen=True), {'buttons': []},
                   {'buttons': snapshot('Start')['buttons'] + snapshot('Stop')['buttons']}]
        for sample in samples:
            with self.subTest(sample=sample), self.assertRaises(RuntimeError):
                action_plan('record', sample)

    def test_missing_invoke_support_does_not_produce_an_action(self):
        with self.assertRaises(RuntimeError):
            action_plan('record', snapshot('Start', invoke_supported=False))

    def test_hover_mode_blocks_invoke_before_accessing_the_window(self):
        ui = RecordingUI(None, None, read_only=True)
        with self.assertRaises(RuntimeError):
            ui.request('invoke', 'Start')

    def test_read_only_mode_forces_command_planning_without_an_action(self):
        ui = RecordingUI(None, None, read_only=True)
        ui.read = lambda: snapshot('Start')
        ui.request = lambda *args, **kwargs: self.fail('Read-only mode sent an action request.')
        result = ui.run('record')
        self.assertTrue(result['dry_run'])
        self.assertEqual(result['state'], 'stopped')


if __name__ == '__main__':
    unittest.main()
