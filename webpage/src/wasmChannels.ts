import { useEffect, useState } from "react";

export type WasmChannels = { stable: boolean; unstable: boolean };

export const defaultWasmChannel =
  import.meta.env.VITE_WASM_DEFAULT_CHANNEL === "unstable" ? "unstable" : "stable";

export function useWasmChannels() {
  const [channels, setChannels] = useState<WasmChannels>({ stable: false, unstable: false });

  useEffect(() => {
    let active = true;
    void fetch("wasm/channels.json")
      .then((response) => {
        if (!response.ok) throw new Error("WASM channel metadata unavailable");
        return response.json() as Promise<WasmChannels>;
      })
      .then((available) => {
        if (active) setChannels(available);
      })
      .catch(() => {
        // Local website builds can run without deployed WASM packages.
      });
    return () => {
      active = false;
    };
  }, []);

  return channels;
}
