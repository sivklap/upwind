import os
import sys
import json
import traceback
import boto3
from dotenv import load_dotenv

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from utils import read_file
from llms.base_classifier import IAMClassifier

load_dotenv()

class BedrockClassifier(IAMClassifier):
    def __init__(self):
        super().__init__()
        self.model_id = os.environ.get("BEDROCK_MODEL_ID")
        self.region = os.environ.get("AWS_REGION")
        self.system_instruction = read_file(os.environ["LLM_INITIAL_PROMPT_PATH"])
        
        if os.getenv("AWS_BEDROCK_BEARER_TOKEN"):
            os.environ["AWS_BEARER_TOKEN_BEDROCK"] = os.environ["AWS_BEDROCK_BEARER_TOKEN"]

        self.client = boto3.client("bedrock-runtime", region_name=self.region)

    def test_connection(self) -> bool:
        try:
            response = self.client.converse(
                modelId=self.model_id,
                messages=[
                    {
                        "role": "user",
                        "content": [{"text": "Ping"}]
                    }
                ]
            )
            print("Bedrock connection successful.")
            return True
        except Exception as e:
            print(f"Bedrock connection failed: {e}")
            traceback.print_exc()
            return False

    def classify_policy(self, policy_json: dict) -> dict | None:
        try:
            user_prompt = f"Here is the IAM policy:\n{json.dumps(policy_json, indent=2)}\nReturn only the JSON output."
            full_prompt = f"{self.system_instruction}\n{user_prompt}"
            response = self.client.converse(
                modelId=self.model_id,
                messages=[
                    {
                        "role": "user",
                        "content": [{"text": full_prompt}]
                    }
                ]
            )
            text_output = response["output"]["message"]["content"][0]["text"]
            return json.loads(text_output[text_output.find("{"):])
        except Exception as e:
            print(f"Error during classification: {e}")
            traceback.print_exc()
            return None

if __name__ == "__main__":
    BedrockClassifier().main()