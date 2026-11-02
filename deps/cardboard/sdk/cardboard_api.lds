VERS_1.0 {
  global:
    # Export Cardboard Symbols.
    Cardboard*;
    *nativeIncrementDeviceParamsChangedCount;
    *JNIEnv*;
    *JavaVM*;
    *Unity*;

  # Hide everything else.
  local:
    *;
};
