"""Class demo: summarize student grades with lists, dictionaries, and functions."""

grades = {
    "Alice": [88, 92, 79],
    "Ben": [72, 65, 80],
    "Chloe": [95, 98, 91],
    "Daniel": [58, 70, 64],
}


def average(scores):
    """Return the mean of a list of numbers."""
    return sum(scores) / len(scores)


def letter(score):
    """Convert a numeric score to a letter grade."""
    if score >= 90:
        return "A"
    if score >= 80:
        return "B"
    if score >= 70:
        return "C"
    if score >= 60:
        return "D"
    return "F"


def report(table):
    """Print one line per student, best average first."""
    ranked = sorted(table.items(), key=lambda item: average(item[1]), reverse=True)
    for name, scores in ranked:
        mean = average(scores)
        print(f"{name:<8} {mean:6.1f}  {letter(mean)}")


if __name__ == "__main__":
    report(grades)
    class_mean = average([average(s) for s in grades.values()])
    print(f"Class average: {class_mean:.1f}")
