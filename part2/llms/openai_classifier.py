import os
import sys
import json
import traceback
from dotenv import load_dotenv
from openai import OpenAI
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from utils import read_file
from llms.base_classifier import IAMClassifier

load_dotenv()

class OpenAIClassifier(IAMClassifier):
    def __init__(self):
        super().__init__()
        self.client = OpenAI(api_key=os.environ.get("OPENAI_API_KEY"))
        self.system_instruction = read_file(os.environ["LLM_INITIAL_PROMPT_PATH"])

    def test_connection(self) -> bool:
        try:
            response = self.client.chat.completions.create(
                model="gpt-3.5-turbo",
                messages=[{"role": "user", "content": "Ping"}],
                max_tokens=10
            )
            if response:
                print("OpenAI connection successful.")
                return True
        except Exception as e:
            print(f"OpenAI connection failed: {e}")
            traceback.print_exc()
            return False

    def classify_policy(self, policy_json: dict) -> dict | None:
        user_prompt = f"Here is the IAM policy:\n{json.dumps(policy_json, indent=2)}\nReturn only the JSON output."
        try:
            response = self.client.chat.completions.create(
                model="gpt-3.5-turbo",
                messages=[
                    {"role": "system", "content": self.system_instruction},
                    {"role": "user", "content": user_prompt}
                ],
                temperature=0.2,
                max_tokens=300
            )
            reply = response.choices[0].message.content
            return json.loads(reply[reply.find("{"):])
        except Exception as e:
            print(f"Error during classification: {e}")
            traceback.print_exc()
            return None

if __name__ == "__main__":
    OpenAIClassifier().main()
