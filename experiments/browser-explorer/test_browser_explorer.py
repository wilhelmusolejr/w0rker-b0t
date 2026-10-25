import copy
import random
import unittest

from browser_explorer import START_URL, Explorer, SiteScope, allowed_url, classify, eligible, signature
from motion import ScrollRhythm, click_point, pointer_path, rest_point


def state(url=START_URL):
    return {'url': url, 'title': 'A page', 'headings': ['Heading'], 'overlays': [],
            'tabs': [{'id': 'tab1'}], 'controls': [
                {'id': 'button1', 'name': 'Show more', 'type': 'Button', 'value': '',
                 'expanded': 'Collapsed', 'selected': False, 'enabled': True,
                 'offscreen': False, 'scope': '', 'left': 10, 'top': 20}]}


class BrowserPolicyTests(unittest.TestCase):
    def test_websites_are_not_limited_to_a_named_list(self):
        for url in (START_URL, START_URL + 'controlflow.html',
                    'https://docs.python.org/3/library/json.html',
                    'https://github.com/eltociear?tab=repositories',
                    'https://github.com/eltociear/translations/issues/12',
                    'https://example.org/', 'https://news.example.net/articles/42',
                    'http://127.0.0.1:8765/index.html',
                    'https://store.example/products', 'https://blog.example/archive/2025'):
            with self.subTest(url=url):
                self.assertTrue(allowed_url(url))

    def test_auth_mutation_and_download_routes_are_excluded(self):
        for url in ('javascript:alert(1)', 'https://github.com/login',
                    'https://github.com/eltociear/repo/issues/new',
                    'https://github.com/eltociear/repo/edit/main/README.md',
                    'https://github.com/eltociear/repo/archive/main.zip',
                    'https://github.com/eltociear/repo/releases/download/v1/app.exe',
                    'https://github.com/eltociear/%65dit/file',
                    'https://docs.python.org/3/archives/python.zip',
                    'https://docs.python.org/3/../admin/',
                    'https://site.example/repo%2Fdelete%2Fitem',
                    'https://site.example/repo%252Fdelete%252Fitem',
                    'https://shop.example/cart?action=checkout',
                    'https://github.com/eltociear/repo?quick_pull=1'):
            with self.subTest(url=url):
                self.assertFalse(allowed_url(url))

    def test_invalid_addresses_and_credentials_are_rejected(self):
        for url in ('data:text/html,hello', 'file:///C:/test.html', 'https://user:pass@example.org/',
                    'https://example.org:bad/', 'https://example.org:0/', 'https://example.org:99999/',
                    'https://exa mple.org/', 'https://example.org/\npath', 'https://example.org\\elsewhere'):
            with self.subTest(url=url):
                self.assertFalse(allowed_url(url))

    def test_scope_allows_www_and_https_redirects_but_not_other_websites(self):
        scope = SiteScope('http://example.org/')
        for url in ('https://example.org/article', 'https://www.example.org/article',
                    'http://example.org:80/article'):
            self.assertTrue(allowed_url(url, scope))
        for url in ('https://example.org.evil.test/', 'https://other.example.org/',
                    'https://www.evil.test/example.org', 'https://example.org:8443/'):
            self.assertFalse(allowed_url(url, scope))
        self.assertTrue(SiteScope('https://www.example.org/').contains('https://example.org/'))

    def test_custom_ports_and_unicode_hosts_preserve_the_website_boundary(self):
        scope = SiteScope('http://127.0.0.1:8765/')
        self.assertTrue(scope.contains('http://127.0.0.1:8765/page'))
        self.assertFalse(scope.contains('http://127.0.0.1:9999/page'))
        self.assertFalse(scope.contains('http://www.127.0.0.1:8765/'))
        self.assertTrue(SiteScope('https://täst.example/').contains('https://xn--tst-qla.example/page'))

    def test_relative_links_are_scoped_to_the_supplied_website(self):
        control = {**state()['controls'][0], 'type': 'Hyperlink', 'name': 'Read an article', 'value': '../article'}
        self.assertTrue(eligible(control, 'https://example.org/topics/start'))
        self.assertTrue(eligible({**control, 'value': '#section'}, 'https://example.org/topics/start'))
        self.assertFalse(eligible({**control, 'value': '//elsewhere.example/article'}, 'https://example.org/topics/start'))
        self.assertFalse(eligible({**control, 'value': ''}, 'https://example.org/topics/start'))

    def test_generic_navigation_controls_need_no_website_specific_names(self):
        control = {**state()['controls'][0], 'name': 'Chapters', 'expanded': ''}
        self.assertTrue(eligible({**control, 'navigation': True}, START_URL))
        self.assertTrue(eligible({**control, 'type': 'TabItem'}, START_URL))
        self.assertFalse(eligible(control, START_URL))
        self.assertFalse(eligible({**control, 'navigation': True, 'in_form': True}, START_URL))
        self.assertTrue(eligible({**control, 'name': 'Cancel', 'in_form': True}, START_URL))

    def test_mutating_buttons_and_background_controls_are_not_candidates(self):
        control = state()['controls'][0]
        for name in ('Star', 'Unstar', 'Follow', 'Report abuse', 'Delete file', 'Download ZIP'):
            self.assertFalse(eligible({**control, 'name': name}, START_URL))
        self.assertTrue(eligible(control, START_URL))
        self.assertFalse(eligible(control, START_URL, [{'id': 'dialog1'}]))
        self.assertTrue(eligible({**control, 'scope': 'dialog1'}, START_URL, [{'id': 'dialog1'}]))

    def test_a_fresh_relative_destination_matches_the_selected_link(self):
        checker = object.__new__(Explorer)
        checker.scope = SiteScope('https://example.org/')
        selected = {'type': 'Hyperlink', 'value': 'https://example.org/article'}
        checker.verify_click_policy({'url': 'https://example.org/'}, selected,
                                    {'type': 'Hyperlink', 'value': '/article'})

    def test_changed_destinations_are_rejected_before_clicking(self):
        checker = object.__new__(Explorer)
        checker.scope = SiteScope('https://example.org/')
        selected = {'type': 'Hyperlink', 'value': 'https://example.org/article'}
        for value in ('https://outside.example/article', '/different-article', '/delete/item', ''):
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                checker.verify_click_policy({'url': 'https://example.org/'}, selected,
                                            {'type': 'Hyperlink', 'value': value})

    def test_changed_control_types_are_rejected_before_clicking(self):
        checker = object.__new__(Explorer)
        checker.scope = SiteScope('https://example.org/')
        with self.assertRaises(RuntimeError):
            checker.verify_click_policy({'url': 'https://example.org/'},
                                        {'type': 'Hyperlink', 'value': '/article'},
                                        {'type': 'Button', 'value': ''})


class OutcomeTests(unittest.TestCase):
    def test_navigation_anchor_and_same_url_content_change_are_distinct(self):
        before = state()
        self.assertEqual(classify(before, state(START_URL + '?tab=repositories')), 'navigation')
        self.assertEqual(classify(before, state(START_URL + '#readme')), 'page-anchor')
        after = copy.deepcopy(before)
        after['headings'] = ['Different content']
        self.assertEqual(classify(before, after), 'content-changed')

    def test_modal_precedes_content_change_and_close_is_distinct(self):
        before = state()
        after = copy.deepcopy(before)
        after['overlays'] = [{'id': 'dialog1'}]
        after['headings'] = ['Dialog title']
        self.assertEqual(classify(before, after), 'overlay-opened')
        self.assertEqual(classify(after, before), 'overlay-closed')

    def test_expansion_selection_and_new_tab(self):
        before = state()
        after = copy.deepcopy(before)
        after['controls'][0]['expanded'] = 'Expanded'
        self.assertEqual(classify(before, after, before['controls'][0]), 'section-state-changed')
        after = copy.deepcopy(before)
        after['controls'][0]['selected'] = True
        self.assertEqual(classify(before, after, before['controls'][0]), 'selection-changed')
        after['tabs'].append({'id': 'tab2'})
        self.assertEqual(classify(before, after), 'new-tab')

    def test_runtime_identity_and_position_alone_do_not_mean_new_content(self):
        before = state()
        after = copy.deepcopy(before)
        after['controls'][0].update(id='rerendered', left=400, top=800)
        self.assertEqual(signature(before), signature(after))
        self.assertEqual(classify(before, after), 'no-observable-change')

    def test_visible_text_changes_are_observable_without_url_changes(self):
        before = state()
        after = copy.deepcopy(before)
        before['visible_text'] = 'The original article.'
        after['visible_text'] = 'Newly revealed article text.'
        self.assertEqual(classify(before, after), 'content-changed')


class MotionTests(unittest.TestCase):
    def test_short_moves_are_quick_and_long_moves_take_longer(self):
        short = pointer_path((100, 100), (120, 100), random.Random(4))
        long = pointer_path((100, 100), (1500, 900), random.Random(4))
        self.assertLess(short.duration, 0.35)
        self.assertGreater(long.duration, short.duration * 2)

    def test_varied_paths_still_land_exactly_on_the_target(self):
        for seed in range(25):
            path = pointer_path((20, 40), (900, 600), random.Random(seed))
            self.assertEqual(path.at(0), (20, 40))
            self.assertEqual(path.at(1), (900, 600))
            previous = -1
            for index in range(101):
                x, y = path.at(index / 100)
                progress = (x - 20) * 880 + (y - 40) * 560
                self.assertGreaterEqual(progress + 1e-6, previous)
                previous = progress

    def test_resting_points_vary_within_the_content(self):
        region = {'left': 100, 'top': 200, 'right': 1300, 'bottom': 1000}
        rng = random.Random(9)
        points = [rest_point(region, rng) for _ in range(80)]
        self.assertGreater(len(set(points)), 60)
        for x, y in points:
            self.assertTrue(100 < x < 1300 and 200 < y < 1000)

    def test_click_variation_stays_inside_small_and_offset_controls(self):
        rng = random.Random(9)
        for region in ({'left': -20, 'top': 200, 'right': -16, 'bottom': 204},
                       {'left': 100, 'top': 80, 'right': 360, 'bottom': 105}):
            points = [click_point(region, rng) for _ in range(40)]
            self.assertGreater(len(set(points)), 1)
            for x, y in points:
                self.assertTrue(region['left'] <= x < region['right'])
                self.assertTrue(region['top'] <= y < region['bottom'])

    def test_scroll_passes_mix_fast_scanning_and_slow_reading(self):
        rhythm = ScrollRhythm(random.Random(9))
        for _ in range(25):
            plan = rhythm.plan(40)
            self.assertEqual({burst.style for burst in plan.bursts}, {'fast', 'slow'})
            self.assertTrue(2 <= len(plan.bursts) <= 4)
            self.assertTrue(all(step.delta for burst in plan.bursts for step in burst.steps))

    def test_fast_bursts_are_larger_and_faster_than_slow_bursts(self):
        rhythm = ScrollRhythm(random.Random(17))
        for _ in range(25):
            bursts = rhythm.plan(40).bursts
            fast = [b for b in bursts if b.style == 'fast']
            slow = [b for b in bursts if b.style == 'slow']
            self.assertGreater(min(b.notches for b in fast), max(b.notches for b in slow))
            self.assertLess(max(s.delay for b in fast for s in b.steps),
                            min(s.delay for b in slow for s in b.steps))

    def test_scrolling_recovers_at_page_ends(self):
        rhythm = ScrollRhythm(random.Random(17))
        self.assertEqual(rhythm.plan(0, stalls=4).bursts[0].direction, -1)
        self.assertEqual(rhythm.plan(100).bursts[0].direction, 1)
        self.assertEqual(rhythm.plan(50, stalls=4).bursts[0].direction, 1)


if __name__ == '__main__':
    unittest.main()
