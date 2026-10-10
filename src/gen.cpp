// lg-gen: write a seeded synthetic ITCH 5.0 session in BinaryFILE framing.
//   lg-gen --out data/synth.itch [--seed 42] [--messages 1000000] [--symbols 4]
#include <cstdio>
#include <fstream>

#include "lg/synth.hpp"
#include "lg/util.hpp"

int main(int argc, char** argv) {
  lg::Args args(argc, argv);
  const std::string out = args.get("--out");
  if (out.empty()) {
    std::fprintf(stderr, "usage: lg-gen --out FILE [--seed N] [--messages N] [--symbols N]\n");
    return 2;
  }
  lg::SynthConfig cfg;
  cfg.seed = args.num("--seed", cfg.seed);
  cfg.messages = args.num("--messages", cfg.messages);
  cfg.symbols = uint16_t(args.num("--symbols", cfg.symbols));

  lg::SynthCounts c;
  const auto bytes = lg::generate_session(cfg, &c);
  std::ofstream f(out, std::ios::binary);
  f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
  if (!f) {
    std::fprintf(stderr, "write failed: %s\n", out.c_str());
    return 1;
  }
  std::fprintf(stderr,
               "wrote %s: %zu bytes, seed %llu, %llu order messages "
               "(add %llu, cancel %llu, delete %llu, execute %llu, replace %llu)\n",
               out.c_str(), bytes.size(), (unsigned long long)cfg.seed,
               (unsigned long long)cfg.messages, (unsigned long long)c.add,
               (unsigned long long)c.cancel, (unsigned long long)c.remove,
               (unsigned long long)c.execute, (unsigned long long)c.replace);
  return 0;
}
