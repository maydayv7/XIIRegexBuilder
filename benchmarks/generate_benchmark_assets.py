import random
import string
import sys
import os

class RegexGenerator:
    def __init__(self, alphabet=string.ascii_lowercase + string.digits):
        self.alphabet = alphabet
        self.max_depth = 3

    def generate(self, depth=0):
        if depth >= self.max_depth or random.random() < 0.3:
            # Generate a literal or a dot
            if random.random() < 0.1:
                return "."
            else:
                length = random.randint(1, 4)
                return "".join(random.choices(self.alphabet, k=length))

        choice = random.random()
        if choice < 0.25: # Quantifier
            inner = self.generate(depth + 1)
            # Ensure we don't apply quantifier to nothing or multiple quantifiers
            if len(inner) > 1:
                inner = f"({inner})"
            return f"{inner}{random.choice(['*', '+', '?'])}"
        elif choice < 0.5: # Union
            left = self.generate(depth + 1)
            right = self.generate(depth + 1)
            return f"({left}|{right})"
        else: # Concatenation
            left = self.generate(depth + 1)
            right = self.generate(depth + 1)
            return f"{left}{right}"

class StringGenerator:
    def __init__(self, alphabet=string.ascii_lowercase + string.digits):
        self.alphabet = alphabet

    def sample_from_regex(self, regex_str):
        """
        Naive sampling from a regex string. 
        This handles the supported subset: . * + ? | ( )
        """
        # Very simplified recursive-descent style sampler
        # For a better approach, we'd parse the regex into an AST, 
        # but for a benchmark asset generator, a greedy substitution is often enough.
        
        # 1. Handle unions (pick one side)
        while '(' in regex_str and '|' in regex_str:
            # Find the innermost matching parens with a pipe
            start = -1
            pipe = -1
            for i, c in enumerate(regex_str):
                if c == '(': start = i
                elif c == '|': pipe = i
                elif c == ')':
                    if start != -1 and pipe != -1 and start < pipe < i:
                        left = regex_str[start+1:pipe]
                        right = regex_str[pipe+1:i]
                        choice = random.choice([left, right])
                        regex_str = regex_str[:start] + choice + regex_str[i+1:]
                        break
            else: break # No more unions found

        # 2. Handle quantifiers
        # This is tricky with raw strings. Let's do a simple pass for basic ones.
        res = []
        i = 0
        while i < len(regex_str):
            char = regex_str[i]
            next_char = regex_str[i+1] if i + 1 < len(regex_str) else ""
            
            if next_char in ['*', '+', '?']:
                count = 0
                if next_char == '*': count = random.randint(0, 3)
                elif next_char == '+': count = random.randint(1, 3)
                elif next_char == '?': count = random.randint(0, 1)
                
                # If char was dot, pick random
                actual_char = random.choice(self.alphabet) if char == '.' else char
                res.append(actual_char * count)
                i += 2
            elif char == '.':
                res.append(random.choice(self.alphabet))
                i += 1
            elif char in '()|':
                # Stripping remaining meta-chars from the sample
                i += 1
            else:
                res.append(char)
                i += 1
        
        return "".join(res)

    def generate_random(self, length_range=(1, 15)):
        return "".join(random.choices(self.alphabet, k=random.randint(*length_range)))

def main():
    num_regex = int(sys.argv[1]) if len(sys.argv) > 1 else 70
    num_strings = int(sys.argv[2]) if len(sys.argv) > 2 else 10000
    
    output_dir = "benchmarks/assets"
    os.makedirs(output_dir, exist_ok=True)
    
    regex_file = os.path.join(output_dir, "bench_regexes.txt")
    string_file = os.path.join(output_dir, "bench_strings.txt")
    
    re_gen = RegexGenerator()
    str_gen = StringGenerator()
    
    regexes = []
    for _ in range(num_regex):
        regexes.append(re_gen.generate())
        
    with open(regex_file, "w") as f:
        f.write("# Generated General Benchmark Regexes\n")
        for r in regexes:
            f.write(r + "\n")
            
    strings = []
    for _ in range(num_strings):
        if random.random() < 0.4 and regexes:
            # Sample from a random regex to ensure we get matches
            strings.append(str_gen.sample_from_regex(random.choice(regexes)))
        else:
            # Pure random noise
            strings.append(str_gen.generate_random())
            
    # Shuffle to mix hits and misses
    random.shuffle(strings)
            
    with open(string_file, "w") as f:
        f.write("# Generated General Benchmark Test Strings\n")
        for s in strings:
            f.write(s + "\n")
            
    print(f"Generated {num_regex} general regexes in {regex_file}")
    print(f"Generated {num_strings} general strings in {string_file}")

if __name__ == "__main__":
    main()
