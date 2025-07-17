import os
from glob import glob
from collections import defaultdict, Counter
from llms.openai_classifier import OpenAIClassifier
from llms.bedrock_classifier import BedrockClassifier
# from llms.ollama_classifier import OllamaClassifier
from utils import read_JSON_file

def main():
    print("Scanning for policy files...")
    weak_policy_paths = glob("part2/policies/weak/*.json")
    strong_policy_paths = glob("part2/policies/strong/*.json")
    all_policy_paths = weak_policy_paths + strong_policy_paths

    if not all_policy_paths:
        print("Error: No policy files found in 'part2/policies/weak/' or 'part2/policies/strong/'.")
        return

    print(f"Found {len(weak_policy_paths)} weak and {len(strong_policy_paths)} strong policies.")
    print("Initializing models...")

    models = {
        "OpenAI": OpenAIClassifier(),
        "Bedrock": BedrockClassifier(),
        # "Ollama": OllamaClassifier(),
    }

    results = []

    for policy_path in all_policy_paths:
        label = "Weak" if "weak" in policy_path else "Strong"
        policy_json = read_JSON_file(policy_path)
        policy_name = os.path.basename(policy_path)

        print(f"\nProcessing policy: {policy_name} (expected: {label})")

        for model_name, model in models.items():
            print(f"  Model: {model_name}")

            model_result = {
                "model": model_name,
                "policy": policy_name,
                "expected": label,
                "classification": None,
                "match": None,
                "reason": None
            }

            try:
                result = model.classify_policy(policy_json)
                if result:
                    classification = result.get("classification")
                    match = classification == label

                    model_result["classification"] = classification
                    model_result["match"] = match
                    model_result["reason"] = result.get("reason")

                    print(f"    Classified as: {classification} | Match: {match}")
                else:
                    print("    Warning: Model returned no result.")
            except Exception as e:
                model_result["classification"] = "Error"
                model_result["reason"] = str(e)
                print(f"    Error during classification: {e}")

            results.append(model_result)

    # Summary
    summary = defaultdict(lambda: {"correct": 0, "wrong": 0, "reasons": []})

    for r in results:
        if r["classification"] in ["ConnectionFailed", "Error"]:
            continue
        if r["match"]:
            summary[r["model"]]["correct"] += 1
        else:
            summary[r["model"]]["wrong"] += 1
            if r["reason"]:
                summary[r["model"]]["reasons"].append(r["reason"])

    print("\n=== Classification Summary ===")
    for model_name, stats in summary.items():
        total = stats["correct"] + stats["wrong"]
        accuracy = stats["correct"] / total if total > 0 else 0
        print(f"\nModel: {model_name}")
        print(f"  Correct: {stats['correct']}")
        print(f"  Misclassified: {stats['wrong']}")
        print(f"  Accuracy: {accuracy:.2%}")

        if stats["reasons"]:
            reason_counts = Counter(stats["reasons"])
            print("  Most common reasons for misclassification:")
            for reason, count in reason_counts.most_common(5):
                print(f"    - {reason} ({count})")
        else:
            print("  No reasons provided for misclassifications.")

if __name__ == "__main__":
    main()
