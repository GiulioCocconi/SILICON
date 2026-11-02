export default {
  multipass: true,
  js2svg: { pretty: true, indent: 2 },
  plugins: [
    'cleanupAttrs',
    'removeEditorsNSData',
    'removeUselessDefs',
    { name: 'cleanupNumericValues', params: { floatPrecision: 5 } },
    'convertShapeToPath',
    {
      name: 'convertPathData',
      params: { floatPrecision: 5, applyTransforms: false },
    },
    'sortAttrs',
  ],
};
