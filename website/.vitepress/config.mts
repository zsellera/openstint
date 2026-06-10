import { defineConfig } from 'vitepress'

// https://vitepress.dev/reference/site-config
export default defineConfig({
  vite: {
    resolve: { preserveSymlinks: true },
  },
  lang: 'en-US',
  title: "OpenStint",
  description: "OpenStint is an open-source project reading AMB/RC3/RC4-style near-field transponders using inexpensive SDR (HackRF or RTL-SDR) radios.",
  head: [
    ['link', { rel: 'icon', type: 'image/svg+xml', href: '/logo.svg' }],
    ['link', { rel: 'icon', type: 'image/png', sizes: '32x32', href: '/favicon-32.png' }],
    ['link', { rel: 'icon', type: 'image/png', sizes: '16x16', href: '/favicon-16.png' }],
    ['link', { rel: 'icon', href: '/favicon.ico' }],
    ['link', { rel: 'apple-touch-icon', href: '/apple-touch-icon.png' }],
  ],
  themeConfig: {
    // https://vitepress.dev/reference/default-theme-config
    logo: '/logo.svg',

    nav: [
      { text: 'Home', link: '/' },
      { text: 'Tutorials', link: '/docs/setup-simple-rtlsdr' },
      { text: 'Buy Transponders', link: '/docs/purchase-transponder' }
    ],

    sidebar: [
      {
        text: 'Tutorials',
        items: [
          { text: 'Simple setup /w RTL-SDR', link: '/docs/setup-simple-rtlsdr' },
          { text: 'Supported SDRs', link: '/docs/supported-hardware' },
          { text: 'Loop (the antenna)', link: '/docs/setup-tutorial' },
          { text: 'OpenStint on Windows', link: '/docs/setup-tutorial-windows' },
          { text: 'OpenStint on Raspberry Pi', link: '/docs/setup-tutorial-raspberry' }
        ]
      },
      {
        text: 'Software',
        items: [
          { text: 'LapBeeps', link: '/docs/scoring-lapbeeps' },
          { text: 'RCGTiming', link: '/docs/scoring-rcgtiming' },
          { text: 'ZRound', link: '/docs/scoring-zround' }
        ]
      },
      {
        text: 'Integrations',
        items: [
          { text: 'Decoder protocol', link: '/docs/decoder-protocol' },
          { text: 'Transponder protocol', link: '/docs/transponder-protocol' }
        ]
      },
      {
        text: 'Features',
        items: [
          { text: 'RC4 learning', link: '/docs/rc4' },
          { text: 'Replay capture', link: '/docs/replay-capture' },
          { text: 'Passing detection', link: '/docs/passing-detection' }
        ]
      }
    ],

    socialLinks: [
      { icon: 'github', link: 'https://github.com/zsellera/openstint' }
    ]
  }
})
