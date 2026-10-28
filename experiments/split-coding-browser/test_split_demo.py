import ast
import random
import unittest

from split_demo import SAMPLE, halves, typing_schedule


class SplitPlanTests(unittest.TestCase):
    def test_two_halves_cover_work_area_without_overlap(self):
        work = (0, 0, 1921, 1032)
        code, browser = halves(work, 'left')
        self.assertEqual(code, (0, 0, 960, 1032))
        self.assertEqual(browser, (960, 0, 1921, 1032))
        self.assertEqual(code[2], browser[0])

    def test_coding_can_use_either_side(self):
        left = halves((0, 0, 1920, 1032), 'left')
        right = halves((0, 0, 1920, 1032), 'right')
        self.assertEqual(left, tuple(reversed(right)))

    def test_negative_monitor_coordinates_are_preserved(self):
        code, browser = halves((-1920, -100, 0, 980), 'left')
        self.assertEqual(code, (-1920, -100, -960, 980))
        self.assertEqual(browser, (-960, -100, 0, 980))

    def test_entire_sample_is_scheduled_inside_typing_budget(self):
        for duration in (8, 48, 240):
            plan = typing_schedule(SAMPLE, duration, random.Random(17))
            self.assertEqual(''.join(char for _, char in plan), SAMPLE)
            self.assertTrue(all(a[0] < b[0] for a, b in zip(plan, plan[1:])))
            self.assertAlmostEqual(plan[-1][0], duration)

    def test_sample_is_complete_valid_python(self):
        tree = ast.parse(SAMPLE)
        self.assertTrue(any(isinstance(node, ast.FunctionDef) for node in tree.body))
        self.assertTrue(SAMPLE.endswith('\n'))


if __name__ == '__main__':
    unittest.main()
