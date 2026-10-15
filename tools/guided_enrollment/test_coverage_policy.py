import unittest
from coverage_policy import ENROLL, REGIONS, PRIMARY, keep_new_enrollment, region_results

class CoveragePolicyTests(unittest.TestCase):
    def test_walk_covers_each_guided_region(self):
        self.assertEqual(len(ENROLL),12)
        self.assertEqual(set(ENROLL),set(REGIONS))
        for a,b in zip(ENROLL,ENROLL[1:]):
            ax,ay=REGIONS[a][2]; bx,by=REGIONS[b][2]
            self.assertLessEqual(((ax-bx)**2+(ay-by)**2)**.5,.7)

    def test_all_required_results_pass(self):
        self.assertTrue(keep_new_enrollment(['verify-no-match']*5,['verify-match']*15,['verify-no-match']*3))

    def test_each_missed_position_rejects_candidate(self):
        for i in range(15):
            with self.subTest(i=i):
                after=['verify-match']*15; after[i]='verify-no-match'
                self.assertFalse(keep_new_enrollment(['verify-match']*5,after,['verify-no-match']*3))

    def test_wrong_finger_match_rejects_candidate(self):
        self.assertFalse(keep_new_enrollment(['verify-match']*5,['verify-match']*15,['verify-no-match','verify-match','verify-no-match']))

    def test_missing_and_error_results_reject(self):
        for after in (['verify-match']*14,['verify-match']*14+['timeout']):
            self.assertFalse(keep_new_enrollment(['verify-match']*5,after,['verify-no-match']*3))

    def test_region_results_keep_untested_separate(self):
        result=region_results(['verify-match']*4+['verify-no-match'])
        self.assertEqual(result['center'],{'matched':3,'tested':3})
        self.assertEqual(result['tip'],{'matched':1,'tested':2})
        self.assertEqual(result['base'],{'matched':0,'tested':0})

if __name__=='__main__': unittest.main()
