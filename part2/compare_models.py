import os
from llms.openai_classifier import OpenAIClassifier
from part2.llms.bedrock_classifier import BedrockClassifier
from llms.hugging_face_classifier import HuggingFaceClassifier
from utils import read_JSON_file
from glob import glob

weak_policy_paths = glob("policies/weak/*.json")
strong_policy_paths = glob("policies/strong/*.json")

models = {
    "OpenAI": OpenAIClassifier(),
    "Bedrock": BedrockClassifier(),
    "HuggingFace": HuggingFaceClassifier()
}

results = []

for policy_path in weak_policy_paths + strong_policy_paths:
    label = "Weak" if "weak" in policy_path else "Strong"
    policy_json = read_JSON_file(policy_path)
    policy_name = os.path.basename(policy_path)

    for model_name, model in models.items():
        model_result = {
            "model": model_name,
            "policy": policy_name,
            "expected": label,
            "classification": None,
            "match": None,
            "reason": None
        }

        if not model.test_connection():
            model_result["classification"] = "ConnectionFailed"
        else:
            result = model.classify_policy(policy_json)
            if result:
                model_result["classification"] = result.get("classification")
                model_result["match"] = result.get("classification") == label
                model_result["reason"] = result.get("reason")

        results.append(model_result)