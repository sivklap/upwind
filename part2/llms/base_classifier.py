import sys
import json
import argparse
from abc import ABC, abstractmethod
from utils import read_JSON_file

class IAMClassifier(ABC):
    """
    Abstract base class for IAM policy classifiers.
    """
    
    def __init__(self):
        self.client = None  # Subclasses must initialize this

    @abstractmethod
    def test_connection(self) -> bool:
        """Tests the connection to the LLM API."""
        pass

    @abstractmethod
    def classify_policy(self, policy_json: dict) -> dict | None:
        """Classifies the given IAM policy."""
        pass

    def name(self) -> str:
        return self.__class__.__name__

    def main(self):
        """
        CLI entry point for a classifier.
        """
        parser = argparse.ArgumentParser(description=f"Classify an IAM policy using {self.name()}.")
        parser.add_argument("policy_file", help="Path to the IAM policy JSON file.")
        args = parser.parse_args()

        if not self.test_connection():
            print(f"{self.name()} connection failed.")
            sys.exit(1)

        policy = read_JSON_file(args.policy_file)
        if policy is None:
            print("Failed to read or parse the policy file.")
            sys.exit(1)

        result = self.classify_policy(policy)
        if result:
            print(json.dumps(result, indent=2))
        else:
            print("Classification failed.")
            sys.exit(1)
