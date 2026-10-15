"""Placement guidance and conservative selection; no anatomical coverage estimate."""
REGIONS = {
    'center': ('Center', 'Rest the center of your RIGHT INDEX pad flat, with light pressure.', (0, 0)),
    'tip': ('Tip', 'Move the contact slightly toward the fingertip. Keep overlap with the center; use the pad near the tip, not the nail.', (0, -0.65)),
    'left': ('Left side', 'Roll slightly onto the left side of the pad. Keep the sensor covered and overlap the center.', (-0.65, 0)),
    'right': ('Right side', 'Roll slightly onto the right side of the pad. Keep the sensor covered and overlap the center.', (0.65, 0)),
    'base': ('Base', 'Move contact slightly toward the first crease. Stay on the ridged fingertip pad.', (0, 0.65)),
    'tip-left': ('Tip left', 'Make a small overlapping shift from the tip toward the left side.', (-0.48, -0.48)),
    'base-left': ('Base left', 'Make a small overlapping shift down the left side toward the base.', (-0.48, 0.48)),
    'base-right': ('Base right', 'Make a small overlapping shift along the base toward the right side.', (0.48, 0.48)),
    'tip-right': ('Tip right', 'Make a small overlapping shift up the right side toward the tip.', (0.48, -0.48)),
}
PRIMARY = ['center', 'tip', 'left', 'right', 'base']
BASELINE = PRIMARY
VALIDATION = [key for key in PRIMARY for _ in range(3)]
ENROLL = ['center', 'tip', 'tip', 'tip-right', 'right', 'right',
          'base-right', 'base', 'base-left', 'left', 'tip-left', 'center']

def placement(key):
    name, instruction, _ = REGIONS[key]
    return key, name, instruction

VERIFY_POSITIONS = [placement(key) for key in VALIDATION]
ENROLL_POSITIONS = [placement(key) for key in ENROLL]

def keep_new_enrollment(before, after, negative):
    """Require all five practical regions 3/3 and three confirmed impostor rejects."""
    return (len(before) == 5 and len(after) == 15 and len(negative) == 3
            and all(x in ('verify-match', 'verify-no-match') for x in before)
            and all(x == 'verify-match' for x in after)
            and all(x == 'verify-no-match' for x in negative))

def region_results(after):
    return {key: {'matched': after[i*3:i*3+3].count('verify-match'),
                  'tested': len(after[i*3:i*3+3])} for i, key in enumerate(PRIMARY)}
