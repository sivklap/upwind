import os
import sys
import json
import traceback
from dotenv import load_dotenv
import ollama

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from utils import read_file
from llms.base_classifier import IAMClassifier

load_dotenv()

class OllamaClassifier(IAMClassifier):
    def __init__(self):
        super().__init__()
        self.model_id = os.getenv("OLLAMA_MODEL_ID")
        self.system_instruction = read_file(os.environ["LLM_INITIAL_PROMPT_PATH"])

    def test_connection(self) -> bool:
        try:
            response = ollama.chat(
                model=self.model_id,
                messages=[{"role": "user", "content": "Ping"}]
            )
            print("Ollama connection successful.")
            print("Response:", response["message"]["content"])
            return True
        except Exception as e:
            print("Ollama connection failed:", e)
            traceback.print_exc()
            return False

    def classify_policy(self, policy_json: dict) -> dict | None:
        try:
            user_prompt = f"Here is the IAM policy:\n{json.dumps(policy_json, indent=2)}\nReturn only the JSON output."
            messages = [
                {"role": "system", "content": self.system_instruction},
                {"role": "user", "content": user_prompt}
            ]
            response = ollama.chat(
                model=self.model_id,
                messages=messages
            )
            text_output = response["message"]["content"]
            return json.loads(text_output[text_output.find("{"):])
        except Exception as e:
            print(f"Error during classification: {e}")
            traceback.print_exc()
            return None

if __name__ == "__main__":
    OllamaClassifier().main()
