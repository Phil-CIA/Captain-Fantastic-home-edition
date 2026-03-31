import os

Import("env")


def prepend_package_bin(package_name: str) -> None:
    package_dir = env.PioPlatform().get_package_dir(package_name)
    if not package_dir:
        return

    bin_dir = os.path.join(package_dir, "bin")
    if not os.path.isdir(bin_dir):
        return

    current_path = env["ENV"].get("PATH", "")
    path_entries = current_path.split(os.pathsep) if current_path else []
    if bin_dir not in path_entries:
        env["ENV"]["PATH"] = bin_dir + os.pathsep + current_path if current_path else bin_dir


prepend_package_bin("toolchain-riscv32-esp")
prepend_package_bin("toolchain-esp32ulp")