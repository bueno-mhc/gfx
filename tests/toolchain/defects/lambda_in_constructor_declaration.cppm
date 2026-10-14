// Partition that forward-declares Checked and makes it a friend (as :command_buffer does for
// RenderingPartRecorder).
export module gfx.defects.lambda_in_constructor:declaration;

import std;

export namespace defects
{

class Checked;

class Source
{
public:
    [[nodiscard]] auto code() const noexcept -> int
    {
        return m_code;
    }

private:
    friend class Checked;

    int m_code = 1;
};

} // namespace defects
