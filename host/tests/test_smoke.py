import host


def test_host_package_imports() -> None:
    assert host.__version__ == "0.0.0"
