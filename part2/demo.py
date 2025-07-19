import argparse
import json
from llms.bedrock_classifier import BedrockClassifier
from utils import read_JSON_file

def main():
    parser = argparse.ArgumentParser(description="Classify an IAM policy using BedrockClassifier.")
    parser.add_argument("policy_file", help="Path to the IAM policy JSON file.")
    args = parser.parse_args()

    policy_path = args.policy_file
    policy_json = read_JSON_file(policy_path)

    if policy_json is None:
        print(json.dumps({
            "model": "BedrockClassifier",
            "input_file": policy_path,
            "error": "Failed to read or parse the policy file."
        }, indent=2))
        return

    classifier = BedrockClassifier()
    result = classifier.classify_policy(policy_path)

    if result:
        output = {
            "model": classifier.name(),
            "input_file": policy_path,
            "AI Output": {
                "policy": policy_json,
                "classification": result.get("classification"),
                "reason": result.get("reason")
            }
        }
    else:
        output = {
            "model": classifier.name(),
            "input_file": policy_path,
            "error": "Classification failed or returned no result."
        }

    print(json.dumps(output, indent=2))

if __name__ == "__main__":
    main()
