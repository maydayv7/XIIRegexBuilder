import random
import string

def generate_test_data(filename, count=10000):
    patterns = ["cat", "dog", "apple", "orange", "hello", "end", "1234", "abcd"]
    chars = string.ascii_lowercase + string.digits
    
    with open(filename, "w") as f:
        f.write("# Massive Generated Test Strings\n")
        for _ in range(count):
            if random.random() > 0.5:
                # 50% chance of being a "real" word or pattern
                s = random.choice(patterns) + "".join(random.choices(chars, k=random.randint(0, 10)))
            else:
                # 50% chance of being random noise
                s = "".join(random.choices(chars, k=random.randint(1, 20)))
            f.write(s + "\n")

if __name__ == "__main__":
    generate_test_data("inputs/large_test_strings.txt", 10000)
    print("Generated 10,000 test strings in inputs/large_test_strings.txt")
